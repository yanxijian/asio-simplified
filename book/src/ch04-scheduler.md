# scheduler：handler 队列与 work 计数

第 3 章结束时，scheduler 已经在容器里就位，可它内部还只是一团黑盒。这一章把
黑盒拆开。先看你已经会用的一段代码——第 1 章练习里的那个异常用例，摘自
`tests/asio/io_context.cpp`：

```cpp
ioc.post(&throw_exception);        // throws int
ioc.post(bind(increment, &count)); // counts normally
ioc.post(&throw_exception);

for (;;)
{
  try
  {
    ioc.run();
    break;                         // only a clean exit breaks out
  }
  catch (int)
  {
    ++exception_count;             // some handler threw an int
  }
}
// afterwards: count == 3, exception_count == 2

```

三次 `run()`，两次异常，三个正常 handler 全部执行完。这个用例背后藏着四个
问题：handler 抛异常后，队列里剩下的还跑吗？`run()` 凭什么知道「还有活干，
值得再来一轮」？抛异常时那个「跑了一半的 handler」占用的资源谁收？以及最
根本的——`run()` 到底在循环什么。

`scheduler` 的 83 行 `run_one_impl` 加上三十来行的记账代码（post 计数、
`handler_work`、四条件判定），回答全部四个。

## 设计：一个计数器管住三种活

先给数据结构。scheduler 持有一个队列、一个计数、一个停止标志：

```cpp
std::mutex mutex_;
std::deque<std::function<void()>> queue_;   // handlers waiting to run
std::size_t work_count_ = 0;                // outstanding work units
std::atomic<bool> stopped_{false};

```

三条不变量，在往下读之前先背下来：

1. **每个待办都对应一个工作单位（work），跑完立即释放——handler 抛异常也
   释放**。「待办」在这里专指队列里的 handler；定时器和描述符不占计数，
   但它们同样阻止停止（见不变量 2）；
2. **`work_count_` 为零、队列为空、定时器堆为空、没有已注册描述符——四个
   条件全空时，scheduler 自行 stop**。持有 `io_context::work` 时即使后三者
   全空也不停。stop 之后队列原样保留，`restart()` 才能续跑；
3. **handler 只从队列头部取出执行，每圈至多一个**。执行前设线程标记（dispatch
   的就地判定依赖它），异常沿调用栈向上飞出 `run()`，不在这里捕获。

不变量 1 的「跑完立即释放」由一个 RAII 小类兑现。`handler_work` 的构造什么
都不做（work 单位在 `post` 时已计入），析构时释放：

```cpp
class handler_work
{
public:
  explicit handler_work(scheduler* s) : scheduler_(s) {}
  ~handler_work() { scheduler_->on_work_finished(); }
  // ...
};

```

这个类活多久，work 单位就记多久——它就是 asio 里 `work_cleanup` 的角色。
把 `handler_work` 和 `invoke` 组合起来看：异常从 handler 里飞出时，
`handler_work` 的析构照样执行（栈展开），计数不会漏减；异常继续向上飞出
`run()`，交给调用者。这就是 asio 的行为：**异常归你，记账归框架**。

## 精读：run_one_impl 全文

现在拆那个 83 行的循环。它是全库唯一会阻塞的地方，也是三个服务（队列、
定时器堆、reactor）交汇的地方。逐段来。

**入口：run() 的前置检查。**

```cpp
std::size_t scheduler::run()
{
  if (!has_work())      // all four conditions empty: not worth entering
  {
    stop();
    return 0;
  }
  return run_impl(true);
}

```

空转直接 `stop()` 并返回 0——这也是 asio 的行为：`run()` 发现无事可做，
context 进入 stopped 状态，哪怕你随后又 post 了东西，也要 `restart()` 才肯跑。
`poll()`、`run_one()`、`poll_one()` 的入口都有同样的检查。

**循环第 0 步：先看停止标志。**

```cpp
for (;;)
{
  if (stopped())          // once stopped, no further handler is invoked
    return false;

```

停止的语义是「立即收工，队列原样保留」：已经出队但没执行的 handler 会继续
执行完（当前这一个），没出队的一个不动，等 `restart()`。测试里「异常飞出后
`run()` 重跑、剩余 handler 继续执行」验证的正是这个语义。

**第 1 段：队列里有 handler——取一个，执行一个。**

```cpp
std::function<void()> handler;
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!queue_.empty())
  {
    handler = std::move(queue_.front());
    queue_.pop_front();
  }
}

if (handler)
{
  handler_work work(this);
  invoke(std::move(handler));
  return true;
}

```

注意锁的范围：锁只保护「出队」这个动作，handler 的**执行发生在锁外**。
如果执行也握着锁，handler 里任何一次 `post`（它要拿同一把锁）都会立刻死锁。
`run_one_impl` 返回 true 表示「本轮干了一件活」，`run_impl` 就是一个
`while (run_one_impl(block)) ++count;` 的计数壳。

`handler_work` 在这里构造——出队的瞬间这个 handler 的 work 单位仍然记在账上，
执行完（包括异常）才由析构释放。对照不变量 1。

**第 2 段：队列空了——看定时器。**

```cpp
if (std::shared_ptr<timer_queue::entry> expired =
        timers_.take_expired(clock::now()))
{
  std::function<void(error_code)> complete = std::move(expired->handler);
  if (complete)
  {
    handler_scope scope(this);
    complete(error_code());
  }
  maybe_stop_if_idle();
  return true;
}

```

到期的定时器在 run 循环内**直接调用**它的 handler——回顾第 2 章契约一：
I/O 完成走 post，定时器完成走这条路。`handler_scope` 设线程标记（让
dispatch 的就地判定生效），然后 `complete(error_code())`——空的 error_code，
「正常到期」。调用完顺手 `maybe_stop_if_idle()`：如果这恰好是最后一个待办，
跑完就该停了。

**第 3 段：两个来源都没货——算睡多久。**

```cpp
bool work;
clock::duration timeout = clock::duration::zero();
{
  std::lock_guard<std::mutex> lock(mutex_);
  work = has_work_locked();
  if (work)
  {
    if (!block)                   // poll(): one non-blocking pass only
    {
      if (polled)
        return false;
      timeout = clock::duration::zero();
    }
    else if (timers_.empty())
    {
      timeout = clock::duration::max();   // no timers: sleep indefinitely
    }
    else
    {
      timeout = next timer expiry - now;  // sleep until then
    }
  }
}

if (!work)
{
  maybe_stop_if_idle();           // all four empty: record the stop
  return false;
}

reactor_.run_for(timeout);        // the only place that ever blocks
polled = true;

```

`has_work_locked()` 是不变量 2 的实现，四个条件一次看全：

```cpp
return work_count_ != 0 || !queue_.empty() || !timers_.empty()
    || !reactor_.idle();

```

睡眠时长三选一：没有定时器就无限睡（只能被 `post`、新描述符注册或 `stop`
唤醒——第 5 章会细讲「怎么被叫醒」）；有定时器就睡到最早的那个到期为止；
`poll()` 语义下只做一轮非阻塞巡查（`polled` 标志保证 `poll()` 不会在
reactor 上反复打转）。

最后这一行 `reactor_.run_for(timeout)` 醒来之后，控制流回到循环顶——重新看
停止标志、重新看队列。第 2 章说的「每圈都把三个来源重新检查一遍」，全文
就是这么落的。

## work 的一生

不变量 1 和 2 里反复出现的 work 单位，从生到死只有三种走法：

- **队列**：`post` 时 `++work_count_`，handler 执行完由 `handler_work` 析构
  `--work_count_`；
- **守卫**：`io_context::work w(ctx);` 构造时 `++work_count_`，析构（或
  `reset()`）时 `--work_count_`。它代表「我还要往里塞活，别停」——典型用法
  是多线程服务器里主线程持有一个 work，防止 worker 线程把 context 跑停；
- **异步解析**（第 9 章）：resolver 的工作线程持有它，保证 `getaddrinfo`
  没回来之前 `run()` 不退出。

计数归零时，`on_work_finished()` 立即调用 `maybe_stop_if_idle()`——四条件若
已空，`stopped_` 当场置位，run 循环下一圈开头就会看到。asio 的
`work_finished()` 同样在计数归零时直接 `stop()`。两边行为的差别只在实现的
形状，不在时机。

## 对照表

| 问题 | asio | sasio |
| --- | --- | --- |
| 循环本体 | `detail/impl/scheduler.ipp` 的 `do_run_one()` | `src/scheduler.cpp` 的 `run_one_impl()` |
| work 计数 | `outstanding_work_` | `work_count_` |
| 执行期的记账 | operation 的 `work_cleanup` | `handler_work` RAII |
| 立即完成 | `post_immediate_completion()` | 直接 `post` |
| 停止条件 | `outstanding_work_ == 0`（构造时可选不计） | 四条件（含定时器与描述符） |
| 守卫 | `executor_work_guard` | `io_context::work` |

一处刻意的差异值得点名：asio 的 `run()` 在「只有队列没有定时器」时的判定
路径与 sasio 不同（asio 的队列 handler 也计 work，四条件退化为一条件；
sasio 的定时器与描述符不计数，所以要分开看）。两边的**可观察行为**一致：
只要有任何一种待办，`run()` 不退出。

## 练习

**实验**：制造一次 work 泄漏，亲眼看 `run()` 挂住：

```cpp
sasio::io_context ctx;
sasio::io_context::work guard(ctx);   // forgot to reset
ctx.post([] { std::puts("done"); });
ctx.run();                            // never returns — why?
std::puts("never printed");

```

跑起来（然后 Ctrl+C）。用本章的语言描述卡住的循环：四条件里哪三个空了、
哪个没空、`run_one_impl` 每一圈停在哪一行睡。对照实验：第二个 context
什么都不 post，work 构造后立刻 `reset()`，`run()` 一个 handler 都没跑、
直接返回 **0**：

```cpp
sasio::io_context ctx2;
sasio::io_context::work guard2(ctx2);
guard2.reset();                       // the unit goes back early
std::size_t n = ctx2.run();           // all four empty → stop, returns 0
std::puts(n == 0 ? "clean exit" : "unexpected");

```

两个 context 放在一起看，「work 凭什么让 run() 不退出」和「run() 的返回值
是什么」就都清楚了。

**bug 狩猎**：回到第 1 章留的三个问题，现在你可以自己回答了：

1. 第一个 handler 抛出后，**它自己已经出队**，队列里的正常 handler 原样
   保留——异常飞出 `run()` 后重跑，队列继续；所以 count == 3；
2. `stopped()` 是 **false**：异常不是 `stop()`，四条件没空，停止标志从未
   置位——所以 catch 后直接重跑 `run()` 是合法的；
3. 三轮是因为异常飞出时 `run_impl` 的计数壳被打断，但每轮**至少**处理了一个
   handler；队列里剩余的 handler 会在后续轮次继续跑，直到 3 个正常 handler
   全部执行完、队列空、四条件全空、`run()` 正常返回 0，`break` 才够条件。

对照你当初的预测。如果三问全中，你已经在用实现者的方式思考这个循环了。

---

循环、计数、停止都齐了。但第 3 段里那行 `reactor_.run_for(timeout)` 到现在
还是黑盒——`WSAPoll` 怎么调、描述符表怎么管、别的线程怎么把睡着的人叫醒？
下一章拆 reactor，你会见到「唤醒的两次失守」：两个真实 bug，都埋在这一行里。
