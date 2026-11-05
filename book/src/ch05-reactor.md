# reactor：就绪、唤醒与描述符表

第 4 章的循环里有一行一直没有展开：

```cpp
reactor_.run_for(timeout);   // the only place that ever blocks

```

`run()` 的大多数时间都睡在这一行里。睡在什么上面？醒来靠什么？第 2 章的时序图说「WSAPoll 报告就绪」，但「报告」背后还有一层必须说清的问题——**睡着的人怎么被叫醒**。这一章拆 `src/reactor.cpp`，你会见到全书最精彩的两个 bug：它们不是我们编的教学案例，是这个仓库的真实提交历史里发生过、被测试抓住过的事。

## 设计：一张表加一根「戳一下就醒」的管子

reactor 只有三个成员值得关心：

```cpp
mutable std::mutex mutex_;
std::unordered_map<SOCKET, descriptor_state> descriptors_;  // the table
SOCKET interrupter_ = INVALID_SOCKET;                        // self-connected UDP
std::atomic<bool> interrupted_{false};                       // dedup flag

```

**描述符表**是 reactor 的花名册：每个 socket 一行，记着这个描述符上「谁在读、谁在写」：

```cpp
struct descriptor_state
{
  std::function<void()> read_handler;    // invoked on read-ready (it is do_read)
  std::function<void()> write_handler;   // invoked on write-ready (it is do_write)
  bool empty() const { return !read_handler && !write_handler; }
};

```

注意这里存的是 socket 的**内部回调**（`[this]() { do_read(); }`），不是用户的 handler——用户 handler 还要等 `do_read` 做完真正的系统调用、拿到结果之后，才经 `post` 进队列（第 2 章契约一）。一个方向同时只允许一个登记，与 socket 的「每方向一个挂起操作」规则呼应。

**interrupter** 是一个唤醒 socket。它是一个 UDP socket，`connect` 到它自己的地址上——往里写 1 个字节，它自己立刻变为「可读」。把它混进 `WSAPoll` 的监视列表，任何线程想叫醒睡着的 poller，写一个字节就行。asio 在 POSIX 上用 socket pair，在支持 eventfd 的 Linux 上用 eventfd，Windows 上用 socket pair——sasio 用自连 UDP，省掉一对句柄，效果相同。

两条不变量，先立在这里，本章后半会反复回到它们：

1. **poll 集合是快照。**`WSAPoll` 看到的描述符列表是调用那一刻从表里抄出来的；抄完之后新登记的描述符，本轮看不见。所以**任何改变等待集合的新登记，都必须唤醒正在睡的线程**，让它重新抄一份；
2. **就绪的登记先出表、后执行。**`WSAPoll` 是水平触发：只要数据没被取走，下次 poll 还会报同一个描述符就绪。所以就绪回调被移出表之后才执行——`do_read` 里的 `recv` 若因假警报（spurious readiness）什么都没读到，再重新登记回去。

## 一次唤醒的一生

`post` 是三条唤醒路径里最常用的一条。从 `scheduler::post` 到睡者醒来，全程五步：

1. 线程 B 把 handler 压进队列，出锁，调用 `reactor_.interrupt()`；
2. `interrupt()` 先看原子标志 `interrupted_`：已经是 true 说明有人刚戳过、字节还在路上，直接返回（**去重**——一千个 post 只写一个字节）；
3. 标志从 false 翻成 true，向 interrupter `send` 1 个字节；
4. 线程 A 正睡在 `WSAPoll` 里，poll 列表的最后一项就是 interrupter，字节一到立刻返回；
5. A 处理完就绪的描述符后，把 interrupter 的缓冲区**读空**（`recv` 循环直到读不出东西），标志清回 false——下一轮睡过去，等下一次戳。

去重标志是这套机制的精巧处：唤醒是**电平**语义（有字节就可读），而 `post` 是**边沿**事件（每来一个 handler 戳一次）。没有标志，poll 的读空逻辑和 `interrupt` 的写逻辑会互相赛跑；有了标志，多个 `post` 塌缩成一次唤醒——反正醒来之后循环顶会把队列**整个**看一遍，戳一下和戳十下没有区别。

## 案例专栏：唤醒的两次失守，一次天生的正确

三条唤醒路径（`post`、首次描述符注册、新最早定时器）的来历不是同一条。一条从第一版就写对了，另外两条是测试抓出来的。看懂这组对照，你就懂了「快照模型」的全部代价。

**天生的正确：post。**`scheduler::post` 从第一版起就无条件调用 `reactor_.interrupt()`——队列变了，睡着的线程必须醒来看。这行从未缺失过，因为它和队列在同一个函数里，明显地耦合着。它教的是：**改状态的地方，顺手唤醒**。

**第一次失守：首次描述符注册。**第 2 章给 reactor 加描述符表时，`start_read` 如下：

```cpp
void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  std::lock_guard<std::mutex> lock(mutex_);
  descriptors_[descriptor].read_handler = std::move(handler);
  // no wake-up. The sleeping thread knows nothing of this.
}

```

单线程毫无问题——发起 `async_read_some` 的线程就是 run 循环自己，登记完了下一圈自然会重新抄快照。多线程就会失效：线程 A 睡在 `WSAPoll`（无定时器，超时无穷大），线程 B 对一个新 socket 发起 `async_read_some`——登记进了表，但 A 手里的快照是旧的，也不会有定时器或 post 来补救。**这个读操作永远等不到**。

asio 的 `select_reactor` 早把答案写在 `start_unready_op` 里：

```cpp
bool first = op_queue_[op_type].enqueue_operation(descriptor, op);
scheduler_.work_started();
if (first)
  interrupter_.interrupt();     // ← what sasio was missing

```

`sasio` 的修复（`src/reactor.cpp`）照抄这个形状：

```cpp
void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  bool first = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_state& state = descriptors_[descriptor];
    first = !state.read_handler;      // empty→non-empty in this direction = first
    state.read_handler = std::move(handler);
  }

  // A thread already blocked in WSAPoll() is waiting on a snapshot of the
  // descriptor set taken before this registration, so it must be woken up to
  // rebuild that set. asio does exactly this in
  // select_reactor::start_unready_op():
  //   if (first) interrupter_.interrupt();
  if (first)
    interrupt();
}

```

`first` 的判定在锁内完成（该方向从空到非空），`interrupt()` 在锁外调用——顺序反过来会在持锁时做系统调用，没必要。`start_write` 同样处理。回归测试 `tcp_multithread_wakeup_test` 的判别方式很直接：一个线程睡进 `WSAPoll`，另一个线程注册读并从对端写数据，断言 handler 在 3 秒内执行——没有唤醒时，唯一的唤醒源是 5 秒后的看门狗定时器，测试必失败。

**第二次失守：新最早定时器。**修完描述符，同族问题还剩一处。第 4 章说过，睡多久的公式是「下一个定时器的到期时间减 now」——这个超时**也算快照**：算完之后若有更早的定时器从别的线程加进来，睡着的线程仍按旧时刻醒。更糟的变体是：算出「无限睡」（当时没定时器），随后别的线程 `async_wait` 了一个 100ms 的定时器——没人唤醒，这一睡就没有尽头。

asio 同样早有答案，`select_reactor::schedule_timer`：

```cpp
bool earliest = queue.enqueue_timer(time, timer, op);
if (earliest)
  interrupter_.interrupt();     // new earliest timer → wake and recompute

```

sasio 的修复让 `timer_queue::add` 返回「是否成为新堆顶」，`steady_timer::async_wait` 据此调用 `scheduler::wake_waiters()`（内部就是 `reactor_.interrupt()`）。回归测试 `timer_multithread_wakeup_test`：run 线程先阻塞（无定时器），主线程发起 100ms 等待——修复前 handler 永远不执行，测试以 `fired` 标志失败而非挂起（1 秒后主线程 `stop()` 收尾）。

**三次对照，一个模式。**post 的唤醒是「改状态的地方顺手唤醒」；两次失守分别是「新等待来源」的两种入口（新描述符、新定时器），修复方式都是「成为新的最早等待者 → 唤醒」。说到底，快照模型的总原则只有一句：**任何会让 `WSAPoll` 的答案发生变化的操作，都必须叫醒正在等答案的人。**asio 的 `epoll_reactor` 没有第一个失守——epoll 的描述符集合放在内核，`epoll_wait` 直接看得到新注册，快照问题天然不存在；但定时器那条失守在无 timerfd 的 epoll 上依然存在（`update_timeout()` → `interrupt()`）。平台机制不同，快照的坑是共有的。

## 精读：run_for 的骨架

唤醒的接收端是 `run_for`。全文约 100 行，骨架四段：

```cpp
void reactor::run_for(clock::duration timeout)
{
  std::vector<WSAPOLLFD> poll_fds;        // ① copy the snapshot under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (each row of the descriptor table)
      if (read or write registered)
        { poll_fds.push_back(fd); remember socket; }
    poll_fds.push_back(interrupter);      // the truncheon rides last
  }                                       // ② lock released — poll without it

  const int result = ::WSAPoll(poll_fds.data(), ..., to_milliseconds(timeout));

  std::vector<std::function<void()>> ready_handlers;   // ③ move out under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (each ready descriptor)
      move that direction's callback out of the table into ready_handlers;
  }
  for (auto& handler : ready_handlers)    // ④ execute without the lock
    handler();

  if (interrupter readable) { drain buffer; clear flag; }  // ⑤ tidy up
}

```

四个细节值得单独说：

- **② poll 期间不持锁**。`WSAPoll` 可以睡到无穷大，握着 reactor 的锁睡，其他线程的 `start_read` 全部卡死——这正是死锁的经典配方（等锁的人要唤醒你，你握着锁等人叫）。快照在锁内抄完，poll 在锁外进行，两者中间的时间窗正是「需要唤醒」的原因；
- **③ 移出再执行**。水平触发的 poll 只看缓冲区里有没有数据；如果就绪回调留在表里、`recv` 却晚一步执行，下一圈 poll 会再报一次。把回调移出表（不变量 2），假警报和重复投递都被这个动作挡掉——`do_read` 真读到 `would_block`（假警报）时，会自己重新登记回去；
- **④ 锁外执行**。与 scheduler 的「锁内出队、锁外执行」同一条铁律：回调里会重新登记、会 post，全都可能要拿锁；
- **⑤ 只 drain 自己的**。多个线程同时睡在各自的 `WSAPoll` 上时，同一个字节可能把大家都叫醒；先醒的把缓冲读空，其余线程发现 `recv` 读不出东西，清掉标志继续各自的循环。多醒几次无害，队列本身才是真相的来源——唤醒只是通知，不是数据来源。

`WSAPoll` 报错时的处理也有讲究：`WSAEINTR` 直接返回（系统调用被中断不算失败），其余错误抛 `std::system_error`——教学库不吞错误。

## 对照表

| 问题 | asio（select 后端） | asio（epoll 后端） | sasio |
| --- | --- | --- | --- |
| 等待系统调用 | `select()`（无 IOCP 的 Windows 上为 WSAPoll） | `epoll_wait` | `WSAPoll` |
| 描述符集合 | 用户态快照 | 内核态，注册即生效 | 用户态快照 |
| 新注册要唤醒吗 | 要（`start_unready_op`） | 不用（epoll 天然可见） | 要（`if (first)`） |
| 新最早定时器要唤醒吗 | 要（`schedule_timer`） | timerfd 不用 / 否则要（`update_timeout`） | 要（`add` 返回 earliest） |
| interrupter | socket pair / eventfd | eventfd / socket pair | 自连 UDP socket |
| 源码入口 | `detail/impl/select_reactor.ipp` | `detail/impl/epoll_reactor.ipp` | `src/reactor.cpp` |

一句话：**快照模型的全部复杂度都花在「保持清醒」上**——epoll 把描述符集合放进内核，省掉的正是这部分；select/WSAPoll 路线的每一步唤醒逻辑，asio 与 sasio 一一同构。

## 练习

**实验**：亲手复现第一次失守。打开 `src/reactor.cpp`，注释掉 `start_read` 和 `start_write` 里的两处 `if (first) interrupt();`，重新构建，然后单独运行 `test_ip_tcp.exe`：

```text
cmake --build build --target test_ip_tcp
build\test_ip_tcp.exe

```

可执行文件在 `build\` 下；默认 Visual Studio 生成器则在 `build\Debug\`。

观察 `tcp_multithread_wakeup_test` 的结局：约 5 秒后失败——handler 不是没跑，是晚了 5 秒（看门狗定时器醒的）。把两行注释还原，测试恢复秒过。你刚验证的就是「快照模型的代价」：一行唤醒的缺失，等于把一次毫秒级投递退化成「等下一个偶然事件」。

**bug 狩猎**：`src/ip_tcp.cpp` 的 `do_read()` 在 `recv` 返回 `would_block` 时，会重新调用 `reactor_.start_read(...)` 把自己登记回去，而不是报错。先回答：既然 `WSAPoll` 都说「可读」了，`recv` 怎么可能读不到？想通之后再回答：如果不重新登记，这个读操作会处于什么状态？（提示：回调已经被移出了表——不变量 2。）

---

就绪、唤醒、描述符表都齐了，三个唤醒来源还剩一个：时间。第 4 章的循环靠「下一个定时器的到期时间」决定睡多久，那个「下一个」从哪来——最小堆怎么维护、取消怎么做到绝不重入、以及一个被 asio 自家测试抓出来的堆越界 bug。下一章：timer_queue。
