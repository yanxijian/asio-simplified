# strand：一个队列 + 一个 running 标志

第 2 章说过：多线程同时调 `run()` 时，不同 handler 会并发执行。第 4、5 章
给了这套并发的全部零件——两个线程各自睡在 `WSAPoll` 里、各自从队列取货、
各自执行。到这里，代价开始显形：一个 echo 服务器里，同一个 session 的
`do_read` 和 `do_write_some` 可能**同时**跑在两个线程上，`write_offset_`
这样的普通成员立刻成为数据竞争的靶子。

asio 的答案叫 strand：**把一串 handler 标记为「绝不并发」**。sasio 的
`strand` 是绑定在 `io_context` 上的小对象，`include/sasio/strand.hpp` 加
`src/strand.cpp` 一共 272 行——本章拆开它，顺带讲一个刚刚发生的 bug：
本仓库的评审流程在写作期间抓住的 strand 语义偏差，和第 5 章的两次失守
并排躺在提交历史里。

## 设计：队列、标志、排空循环

strand 的内部状态只有三样：

```cpp
scheduler& scheduler;                     // the drain borrows the scheduler
std::mutex mutex;
std::deque<std::function<void()>> queue;  // queued handlers
bool running = false;                     // is a drain loop running

```

两条不变量：

1. **同一 strand 的 handler 不并发，且 `post` 的顺序即执行顺序**。实现方式
   不是锁住 handler 本身，而是**任何时候至多一个排空循环（drain）在跑**：
   `running` 标志就是这把「逻辑锁」；
2. **handler 抛异常不能锁死 strand**。排空循环在异常路径上释放
   `running`、把剩余 handler 重新调度，然后让异常继续向上飞——strand 的
   串行化保证不吞异常，也不因异常而失灵。

注意 `running` 不是 `std::mutex::lock` 那种阻塞语义：拿不到 strand 的
handler 不等待，而是**排队**——这正是「不并发」与「不阻塞调用者」的
兼容点。排队这个动作本身由 strand 内部的 mutex 保护（细粒度、临界区极短），
真正的串行化由 `running` 保证。

## 一次排空的一生

从 `s.post(h)` 开始，h 的一生分四步：

1. **入队**：h 进 `queue`。锁内检查 `running`：如果排空循环没在跑，把它置
   true，并记下「需要调度排空」；
2. **调度**：向 scheduler `post` 一个匿名函数——它只干一件事：调
   `drain()`。这个匿名 handler 像普通 handler 一样排队、执行、占 work
   单位，对 scheduler 来说 strand 没有任何特殊性；
3. **排空**：`drain()` 是个循环——锁内从 `queue` 取一个，锁外执行，取空了
   就把 `running` 置 false 并返回。执行期间新 post 进来的 handler 只进
   队列，被**同一个**循环顺手取走（下一圈）；
4. **收尾**：队列空、`running` 置 false。下一个 post 进来的人看到的是
   「空闲」的 strand，重新走第 1 步。

这个形状的妙处在第 3 步：**排空循环把「执行权」和「队列」绑在一起**。
只要 `running` 是 true，全世界的 `s.post` 都只是往队列里追加——执行权
始终在 drain 手里，顺序自然就是 post 顺序（不变量 1 的后半）。

`drain` 对异常的处理值得单独看：

```cpp
try
{
  execute(std::move(handler));
}
catch (...)
{
  reschedule_after_exception();
  throw;                          // the exception keeps flying out of run()
}

void strand::impl::reschedule_after_exception()
{
  bool more = false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
    if (!queue.empty())
    {
      running = true;
      more = true;
    }
  }
  if (more)
    schedule_drain();
}

```

异常飞出时 `running` 还挂着——若不处理，strand 就永久锁死了。`reschedule_
after_exception` 先释放，再把剩余 handler 重新排空（新的一次 `post`），
最后 `throw` 让异常继续按 asio 的规则飞出 `run()`。测试
`strand_test` 的异常段验证的正是这个：两个抛异常的 handler 夹着三个正常
handler，跑完三轮 `run()` 后 count == 3、exception_count == 2。

## dispatch 的两条路径：shared 判据

strand 有 `post` 和 `dispatch` 两个入口。`post` 语义简单（永远排队）；
`dispatch` 的语义是「能就地就就地」——**但什么叫「能」，strand 和 scheduler
的答案必须一致**。先看代码，`src/strand.cpp` 的 `impl::dispatch`：

```cpp
if (current_strand_impl == this)        // already inside this strand: inline
{
  execute(std::move(handler));
  return;
}

bool run_now = false;
bool schedule = false;
{
  std::lock_guard<std::mutex> lock(mutex);
  // asio: can_dispatch() && !locked_
  if (scheduler.can_dispatch() && !running)
  {
    running = true;
    run_now = true;                     // inline execution
  }
  else
  {
    queue.push_back(std::move(handler));
    if (!running)
    {
      running = true;
      schedule = true;                  // idle strand + outside run: queue + drain
    }
  }
}
// run_now → execute + drain; schedule → schedule_drain()

```

三个分支，判据只有一句话：**调用线程已在 `run()` 里，且目标当前空闲**。
scheduler 的 `dispatch` 看 context（`current_scheduler == this`），strand
看 strand 本身（`current_strand_impl == this`）——判断对象不同，原则相同。
asio 的两处实现分别写作 `can_dispatch()`（scheduler.ipp）和
`can_dispatch() && !locked_`（strand_service.ipp），sasio 与之一一同构。

这个「同一判据」不是天生的，是修出来的。初版 sasio 的 `strand::dispatch`
只看 `running`：strand 空闲就就地执行——**哪怕调用线程根本不在 `run()` 里**。
于是 `strand s(ctx); s.dispatch(h);` 会在 `run()` 尚未启动时就把 h 跑了，
直接违反第 2 章契约二。本仓的评审流程抓住它后，修复对照的正是 asio
`strand_service::do_dispatch` 的 `can_dispatch() && !locked_`，并补了回归
测试：

```cpp
void strand_dispatch_outside_run_test()
{
  io_context ioc;
  strand s(ioc);

  bool ran = false;
  s.dispatch([&ran] { ran = true; });

  ASIO_CHECK(!ran);      // fails before the fix: the old code ran it inline
  ioc.run();
  ASIO_CHECK(ran);       // the handler executed inside run()
}

```

把「就地执行的全部路径」并排放好，作为第 2 章契约一的总账——三条 dispatch
路径，外加定时器到期这条不经队列的特殊路径：

| 路径 | 就地条件 | 判断对象 |
| --- | --- | --- |
| `scheduler::dispatch` | `current_scheduler == this` | context |
| `strand::dispatch` | `can_dispatch() && strand 空闲` | strand |
| `strand::dispatch`（已在 strand 内） | `current_strand_impl == this` | strand |
| 定时器到期（`run_one_impl`） | 由 run 循环直接调用，不经队列 | —— |

嵌套语义也在这张表里：已经持有 strand 的线程再 `dispatch`，必然就地
（asio 同款——测试 `increment_with_lock` 断言的就是它）。

## 精读：thread_local 的两次入栈

`execute` 与 `running_in_this_thread` 共用一个 thread_local 变量：

```cpp
void execute(std::function<void()> handler)
{
  void* previous = current_strand_impl;
  current_strand_impl = this;          // push

  struct restore
  {
    void* previous;
    ~restore() { current_strand_impl = previous; }   // pop
  } guard = {previous};

  handler();
}

```

存的不是「这个线程在哪个 strand 上」而是**栈**——`previous` 保存外层，
析构恢复。为什么必须是栈？因为 handler 里可以再 `dispatch` 一个**另一个**
strand 的 handler：内层 strand 的 `execute` 把变量改成自己，结束时恢复成
外层。嵌套 strand 的 `running_in_this_thread` 由此每层都答对。

对比 asio：它不存指针，而是把 strand 的 impl 挂进 handler 的
associated executor，`running_in_this_thread` 靠 executor 的调用栈比较。
thread_local 栈是 sasio 无 executor 前提下的最小等价物，语义一致：
**「我在哪个 strand 上」是执行时的动态事实，不是编译期的归属**。

## 对照表

| 问题 | asio | sasio |
| --- | --- | --- |
| 形态 | `strand<Executor>` executor 适配器 + `detail/strand_service` | 绑定 io_context 的对象 + `impl` |
| 排空 | `strand_impl::run()`（operation 完成驱动） | `drain()` 循环 |
| 就地判据 | `can_dispatch() && !locked_` | `can_dispatch() && !running` |
| 嵌套识别 | executor 调用栈 | `thread_local` + 保存/恢复 |
| 绑定语法 | `bind_executor(strand, handler)` | `s.post(...)` / 手工包装（见下） |
| 源码入口 | `strand.hpp`、`detail/impl/strand_service.ipp` | `src/strand.cpp` |

「不能自动继承」是 sasio 差异表里最值钱的一条：asio 的
`async_read(socket, buf, bind_executor(strand, h))` 会让**组合操作内部的
每一个中间完成**都经过 strand；sasio 的手工包装只包住你显式包的那一层。
`examples/ex04_bind_executor.cpp` 用 10 行演示了包装的样子，也演示了它
做不到什么——这层缺口的原理（associated executor 的查找链）是第 12 章
的主题。

## 练习

**实验**：跑 `tests/asio/strand.cpp` 里的多线程用例，然后做破坏实验——把
`strand_multithread_test` 里的 `s.post(...)` 全部换成 `ioc.post(...)`（绕过
strand），再跑一次。观察 `max_active` 的断言结果：不用 strand 时，两个
`run()` 线程并发执行的 handler 数峰值不再是 1。这个对比就是 strand 的全部
价值，也是「哪些状态需要 strand 保护」的判别实验。

**bug 狩猎**：本章开头提到的 strand dispatch bug 就躺在仓库历史里。打开
`tests/asio/strand.cpp` 找到 `strand_dispatch_outside_run_test`，把它改名成
一个不会被跑到的新用例，再把 `src/strand.cpp` 的 dispatch 就地条件从
`scheduler.can_dispatch() && !running` 临时改回 `!running`（还原初版的
偏差），重新构建运行。两个断言哪个先失败？失败时 handler 在哪个线程执行了
（在调试器里看一眼调用栈，栈底是 `strand_dispatch_outside_run_test` 而不是
`run`）？改回正确实现。你已经亲手复现了本仓评审流程的第一次抓虫。

---

串行化补齐后，回调风格的 asio 已经完整。剩最后一块现代拼图：把回调写成
直线代码——`co_await`。下一章拆 `awaitable<T>` 和 `co_spawn`：promise、
symmetric transfer、以及「完成 handler 藏在析构函数里」的收尾设计。
