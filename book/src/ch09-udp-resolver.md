# UDP 与 resolver：同一引擎的两种复用

引擎三大件就位、TCP 的两段式立起来之后，一个网络库的骨架其实已经完整。
本章补两块日常拼图，它们各有一层教学价值：

**UDP** 换掉了系统调用（`sendto` / `recvfrom`），却**几乎不动引擎**——同一张
描述符表、同一次 `WSAPoll`、同一套「发起登记、就绪执行、post 投递」。看完
你会确认：第 5 章的 reactor 从设计起就是协议无关的。

**resolver**（域名解析）则相反：`getaddrinfo` 是阻塞调用，没有描述符、没有
就绪可言，reactor 的模型罩不住它。asio 的答案是线程池；sasio 的答案是**每次
查询一个短命线程**。它是全库唯一引入线程的组件，也是「work 单位撑住
`run()`」的最佳教材。

## UDP：换个系统调用而已

`include/sasio/ip/udp.hpp` 的 socket 长这样——和 TCP 版逐字段对照：

```cpp
bool send_pending_ = false;
std::function<void(error_code, std::size_t)> send_handler_;
const_buffer send_buffer_;
endpoint send_destination_;              // the extra one: where to

bool receive_pending_ = false;
std::function<void(error_code, std::size_t)> receive_handler_;
mutable_buffer receive_buffer_;
endpoint* receive_sender_ = nullptr;     // the extra one: write back who sent it

```

API 只有四个异步入口：`async_send_to(buffer, destination, handler)` 和
`async_receive_from(buffer, sender, handler)`（无连接协议没有「连接」概念，
每次收发都带对端地址；也因此没有 connect/accept，槽位从三组减为两组）。

两段式的形状与 TCP 完全同构。以收为例：

- **发起** `start_receive_from`：存缓冲、存 `sender` 指针、登记读就绪、返回；
- **就绪** `do_receive`：`recvfrom` 一次拿完整数据报，把发送方地址经
  `from_sockaddr` 写回 `*receive_sender_`，然后 `complete_receive`；
- **投递**：handler 经 `post` 出去，带字节数和发送方端点。

错误路径也照抄 TCP：假警报重登记、真错误 `complete_receive(ec, 0)`、
`close()` 先对挂起方向 `complete(operation_aborted)`。第 7 章的全部结论在
UDP 上原样成立，一处例外都没有——这正是 reactor 抽象的价值：**协议变了，
引擎没变**。

两个 UDP 特有的注意点：

- **`receive_sender_` 是裸指针**。挂起期间它必须指向有效的 `endpoint`——
  与「缓冲区必须活着」同一条用户责任。数据报到达时写回的是
  「这个数据报从哪来」，不是「这个 socket 连着谁」（UDP 没有连接）；
- **数据报有边界**。`recvfrom` 一次取走一个完整数据报，缓冲区太小会发生截断。
  截断之后是什么行为，两边的答案不同：POSIX 的 `recvfrom` 静默返回截断后的
  字节数；asio 为对齐这个语义，在 `detail/impl/socket_ops.ipp` 里把
  `WSAEMSGSIZE` / `ERROR_MORE_DATA` 显式吞掉（`result = 0`，按成功返回实际
  字节数，错误清零）；sasio 没做这层吞错——`WSAEMSGSIZE` 会以错误码的形式
  交给 handler。这是一处与 asio 的行为差异——和本书其余与 asio 的差异一样，
  将统一收录进附录 E 的差异总表（README 的 `Deliberate limits` 列表在批次 5
  同步补入）；读 asio 的 datagram 代码时，别把「截断当错误」的预期带过去。
  TCP 的「读满 N 字节」逻辑在 UDP 上不适用——没有流，就没有组合操作的
  用武之地。

## resolver：阻塞调用的异步化

`getaddrinfo("example.com", "80")` 可能耗时几百毫秒到几秒（要查 DNS），
期间线程整个卡住。把它塞进 run 循环等于卡死所有 handler；塞进 reactor 等于
无解——它没有描述符。唯一出路：**把它丢给别的线程，自己继续跑**。

sasio 的 `resolver::start_resolve` 全文（这是全库唯一的线程代码，值得整读）：

```cpp
void resolver::start_resolve(const query& q,
    std::function<void(error_code, results)> handler)
{
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_flag_ = cancelled;               // cancel() touches the latest query only
  }

  scheduler* sched = &scheduler_;
  scheduler_.on_work_started();             // ← work unit #1

  try
  {
    std::thread([sched, q, cancelled, handler = std::move(handler)]() mutable {
      error_code ec;
      results resolved = do_resolve(q, ec); // blocking getaddrinfo happens here

      // Decide cancellation once, when the lookup has finished. A cancel()
      // that arrives after this point no longer changes the result, the same
      // way asio leaves an already-completed operation untouched.
      const bool aborted = cancelled->load(std::memory_order_acquire);

      sched->post(
          [sched, handler = std::move(handler), ec,
              resolved = std::move(resolved), aborted]() mutable {
            struct release
            {
              scheduler* target;
              ~release() { target->on_work_finished(); }   // ← the unit returns
            } guard = {sched};

            handler(aborted ? operation_aborted : ec, resolved);
          });
    }).detach();
  }
  catch (...)
  {
    scheduler_.on_work_finished();          // no thread, the unit returns now
    throw;
  }
}

```

五个要点，按时间顺序：

1. **work 单位先于线程成立**。`on_work_started()` 在 `std::thread` 构造**前**
   调用——从这一刻起，四条件里的 `work_count_ != 0` 成立，`run()` 不会因为
   「队列空、无定时器、无描述符」而停止。没有它，线程还在解析、run 循环已经
   退出、context 已销毁，回来 `post` 就是悬空指针；
2. **线程是 detach 的**。它只捕获值（scheduler 指针、query、标志、handler），
   不需要有人 join。代价是一条硬约束：**io_context 必须活得比未完成的
   `async_resolve` 久**——线程里那句 `sched->post(...)` 引用的 scheduler
   指针，在 context 析构后就是悬空的；
3. **取消是采样不是撤回**。`cancel()` 只是把最近一次查询的标志翻成 true；
   线程内的 `getaddrinfo` 无法被中断，只能等它返回。`aborted` 在**线程里、
   解析完成后**采样一次——此刻之后的 `cancel()` 不再改变结果。这与 asio
   「已完成的操作不受取消影响」的语义一致，采样位置就是语义本身；
4. **结果经 `post` 回 run 循环**。线程绝不直接调 handler——它持有的结果
   （错误码 + endpoint 列表）按值捕获进 lambda，投递到队列；
5. **work 归还挂在 post 的 lambda 上**。`release` 守卫的析构在 handler
   执行**之后**触发——解析期间（含 handler 排队等待）work 单位始终在账，
   handler 跑完才放。异常也不漏：RAII 的老把式，第 4 章 `handler_work`
   的跨线程版本。

同步版 `resolve()` 共享同一个 `do_resolve`，失败抛
`std::system_error`（错误码挂在独立的 resolver 类别上，消息来自
`gai_strerror`）。它是教学便利，真实代码请用异步版。

## 对照表

| 问题 | asio | sasio |
| --- | --- | --- |
| 解析执行体 | 常驻线程池（`detail/resolver_service`） | 每查询一个短命 detach 线程 |
| 取消 | 完整取消队列，逐查询撤销 | 采样标志，仅最近一次查询 |
| 查询结果 | `results`（可迭代，带 service 提示排序） | `std::vector<endpoint>`（IPv4） |
| 线程生命周期 | 池随 context 存亡 | detach，靠 work 单位 + 用户保证存活 |
| endpoint 类型 | `udp::endpoint` / `tcp::endpoint` 各一份 | 同一个 `endpoint` 复用 |
| datagram socket | `basic_datagram_socket.hpp` | `src/ip_udp.cpp` |

`udp::endpoint` 复用 `tcp::endpoint` 值得单独记一笔：asio 里它们是两个不同
类型（即使底层都是 IPv4 地址/端口对），编译期就能拦下「把 TCP 端点发给 UDP
socket」的错误。sasio 合成一个——省类型，但把检查推迟到了运行时。这是
「教学版砍掉类型体操」的又一例：砍掉的东西都有名字，登记在差异表里。

resolver 的「每查询一线程」也一样：查询频繁时线程创建开销和不可取消的
`getaddrinfo` 都是真代价，asio 的常驻线程池 + 取消队列是生产级答案。sasio
给的是**最小可懂**版本——线程、work、post 三个概念各出现一次，一眼看穿。

## 练习

**实验**：写一个 UDP echo 服务器，30 行以内：

```cpp
sasio::io_context ctx;
sasio::ip::udp::socket sock(ctx);
sock.open();
sock.bind(sasio::ip::udp::endpoint(5555));        // same port as ex03

char recv_data[1500];                              // one buffer for receiving
sasio::ip::udp::endpoint sender;

std::function<void()> do_receive = [&]() {
  sock.async_receive_from(sasio::buffer(recv_data), sender,
      [&](sasio::error_code ec, std::size_t n) {
        if (!ec)
        {
          auto payload = std::make_shared<std::vector<char>>(
              recv_data, recv_data + n);           // each send gets its own copy
          sock.async_send_to(sasio::buffer(*payload), sender,
              [payload](sasio::error_code, std::size_t) {});
        }
        do_receive();                              // keep receiving
      });
};
do_receive();
ctx.run();

```

注意那个 `payload`：收和发各用一块缓冲不够——`async_send_to` 还没完成时，
下一次 `async_receive_from` 已经登记了，第二个数据报会把共享缓冲里的内容
覆盖掉，正在发送的字节就被截了尾。每次发送拷贝一份、由 lambda 持有
`shared_ptr` 保活，发送完成（handler 析构）才释放。这个手法眼熟吗？第 8 章
`write_op` 的接力棒，小号版本。

用 `nc -u 127.0.0.1 5555` 敲几行验证回声。然后回答：`async_send_to` 和下一次
`async_receive_from` 是**并发的**（一个槽位各占一个方向）——如果 `send_to`
还没完成、对端又发来新数据报，会发生什么？对照 TCP 版 echo（同一方向
必须串行）说明差异的来源。

**bug 狩猎**：两个时序，`aborted` 的值分别是什么？

- 时序 A：`async_resolve` 发起 → 线程还在 `getaddrinfo` 里 → `cancel()` →
  解析完成 → 采样；
- 时序 B：`async_resolve` 发起 → 解析完成 → **采样之后** → `cancel()` →
  handler 执行。

对照 `start_resolve` 的代码逐行确认：A 报 `operation_aborted`，B 报解析
结果。再想一步：如果采样发生在 post 之后的 handler 里（而不是线程里），
时序 A 的取消通知会被推迟多久？——「采样位置就是语义」这句结论，从代码
里自己长出来才算数。

---

到此，TCP、UDP、域名解析、组合操作——一个可用网络库的 API 面全部就位。
但多线程 `run()` 下，两个 handler 同时碰同一个 `session` 的场面迟早出现。
下一章的 strand：一个队列、一个 `running` 标志，把「不并发」变成可组合的
保证。
