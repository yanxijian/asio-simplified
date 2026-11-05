# 协程：把回调写成直线代码

第 8 章的 `write_op` 用 shared_ptr 接力把一层回调粘成了链；可真实的协议代码不止一层——「读消息头、再读消息体、解析、回写、循环」，四五个异步步骤串下来，回调风格如下：

```cpp
void do_read_header(std::shared_ptr<session> self)
{
  async_read(self->socket_, header_buf,
      [self](error_code ec, std::size_t) {
        if (ec) { close(self); return; }
        self->parse_header();
        async_read(self->socket_, body_buf,
            [self](error_code ec, std::size_t) {
              if (ec) { close(self); return; }
              // ...each step deeper, each piece of state further away
            });
      });
}

```

控制流被打散成一层层 lambda：共享状态全靠 `self` 捕获、错误处理每层抄一遍、提前退出的路径要人肉追踪。C++20 协程把同样的逻辑写成直线：

```cpp
sasio::awaitable<void> session(tcp::socket& socket)
{
  char header[16];
  co_await sasio::async_read_some(socket, sasio::buffer(header));
  // parse_header() sits right here
  char body[64];
  co_await sasio::async_read_some(socket, sasio::buffer(body));
  // ...
}

```

本章拆开 `include/sasio/awaitable.hpp` 和 `co_spawn.hpp`，回答三个问题：`co_await` 挂起时发生了什么？协程结束时，完成通知从哪里来？以及为什么 sasio 的协程不需要 asio 那套 completion token 体系。

## 设计：任务类型与它的承诺

`awaitable<T>` 是一个**协程返回类型**——函数只要声明返回它，体内就可以 `co_await` / `co_return`。编译器会把函数体改写成一个大状态机，状态机的簿记规则由 `promise_type` 提供。sasio 的 promise 有五个成员，每个对应协程生命期的一个问题：

```cpp
struct promise_type
{
  std::optional<T> value;        // co_return lands here
  std::exception_ptr error;      // unhandled_exception lands here
  std::function<void(std::exception_ptr, T)> completion;  // top level: who gets the result
  std::coroutine_handle<> continuation;                   // nested: who gets control back
  bool top_level = false;        // set by co_spawn; decides the ending
};

```

三条不变量：

1. **awaitable 独占协程帧**。它是 move-only 的（拷贝被删除），`co_await` 只接受右值——`co_await task` 对具名变量编译不过，要写 `co_await std::move(task)`。这不是炫技：帧的销毁权必须只有一份，两处代码同时认为自己持有它就是 double free；
2. **顶层与嵌套的收尾方式不同**。`co_spawn` 启动的顶层协程在 `final_suspend` **不挂起**——帧当场销毁，完成 handler 在 promise 的析构函数里被调用；被 `co_await` 的嵌套协程在 `final_suspend` 挂起，通过 symmetric transfer 把控制权交还等待者，帧由等待方释放；
3. **异步完成回 run 循环，同步完成回到发起栈**。协程被谁 resume，完成通知就在谁的栈上：挂起过的协程由 engine 的完成投递恢复（在 `run()` 里）；从未挂起、直接 `co_return` 结束的协程，帧在 `co_spawn` 的首次 `resume()` 内当场销毁——完成 handler 在 **co_spawn 的调用栈**上执行，可能在 `run()` 之外。写作时要知道你的 handler 正跑在哪个栈上。

## 一次协程的一生

用测试里的真实例子 trace 全程：

```cpp
sasio::awaitable<int> return_after_wait(sasio::steady_timer& timer)
{
  co_await sasio::async_wait(timer);
  co_return 42;
}

// in the test:
co_spawn(ioc, return_after_wait(timer),
    [&](std::exception_ptr e, int result) { value = result; });
ioc.run();

```

从 `co_spawn` 到 `42` 落地，八步：

```text
① co_spawn: take the handle, set top_level=true, store completion, resume()
② the body runs → co_await async_wait(timer)
③ async_wait builds async_operation<void>, co_awaits it
    → await_suspend: remember awaiting_ (this coroutine's handle), call start(...)
    → start calls timer.async_wait(handler), registering in the heap
    → async_operation::await_suspend returns → the coroutine suspends,
      control returns to co_spawn
④ co_spawn returns (the coroutine is asleep on the timer)
⑤ the timer expires → the run loop invokes the timer's completion handler
    → the handler calls awaiting_.resume(): the coroutine continues at co_await
⑥ async_operation::await_resume: error_ empty → normal return
⑦ co_return 42 → promise::return_value(42) → final_suspend()
    → final_awaiter::await_ready() == true (top_level) → no suspend
    → the frame destroys on the spot → ~promise_type calls completion(no error, 42)
⑧ the test's lambda receives 42

```

第 ③ 步值得停一下：**协程的挂起点就是「登记一个异步操作」**。`async_operation` 把回调式的发起函数（`timer.async_wait(...)`）包成可 `co_await` 的对象——发起、登记、挂起一气呵成。回调式与协程式的桥只有这一座，桥的两岸是同一个引擎。

## 精读：final_awaiter 与藏在析构里的完成

协程收尾是协程实现里最绕的一段，sasio 把它压到了 20 行：

```cpp
struct final_awaiter
{
  promise_type* self;

  // Top-level coroutines (started by co_spawn) do not suspend at the end:
  // the frame is destroyed automatically and ~promise_type runs the
  // completion handler. Awaited coroutines do suspend and hand control to
  // their awaiting coroutine via symmetric transfer.
  bool await_ready() const noexcept { return self->top_level; }

  std::coroutine_handle<> await_suspend(
      std::coroutine_handle<promise_type> handle) noexcept
  {
    std::coroutine_handle<> next = handle.promise().continuation;
    return next ? next : std::noop_coroutine();
  }

  void await_resume() noexcept {}
};

```

`co_return` 之后编译器调 `final_suspend()`，拿到这个 awaiter：

- **顶层**（`top_level == true`）：`await_ready` 返回 true，跳过挂起——协程体执行完毕、帧**当场销毁**。`~promise_type` 是帧销毁的最后一站，完成通知从那里发出：

  ```cpp
  ~promise_type()
  {
    if (completion)
    {
      try
      {
        completion(error, value ? std::move(*value) : T{});
      }
      catch (...) {}
    }
  }
  
```

  异常和返回值在这里合并成一次调用；completion 内部抛什么都被吞掉——「结果交接」是协程的最后动作，再抛就没有人接了。注意完成 handler 是**就地调用**的：它执行的线程，就是最后一次 resume 协程的线程——而那个 resume 来自 engine 的完成投递（第 ⑤ 步），所以契约二没有被破坏。

- **嵌套**：`await_ready` 返回 false，进入 `await_suspend`——返回值是**下一个要恢复的协程句柄**（等待者），没有等待者就返回 `std::noop_coroutine()`。编译器拿到句柄直接跳过去恢复它，**不新增调用栈层**——这就是 symmetric transfer。没有它，深层嵌套的协程链每返回一层就叠一层栈，链长了会爆栈；有了它，控制权像接力棒一样平移。

为什么顶层要「当场销毁」而不是挂起等人来收？因为顶层没有等待者——除了 completion，没人知道它的存在。挂起一个没人会来恢复的协程，就是泄漏一帧。「不挂起 → 销毁 → 析构函数交结果」把泄漏的口子彻底封住。

## 对照表

| 问题 | asio | sasio |
| --- | --- | --- |
| 任务类型 | `awaitable<T, Executor>`（带 executor） | `awaitable<T>`（无 executor） |
| 帧簿记 | `impl/awaitable.hpp` 的 `awaitable_frame_base`（继承 `awaitable_launch_context`，自定义 `operator new` 回收帧） | `promise_type` + 句柄所有权 |
| 启动 | `co_spawn`（经 executor 投递） | `co_spawn`（直接 `resume()`） |
| 完成通知 | `async_result` 特化 + token（`use_awaitable`） | `promise_type::completion` |
| 挂起恢复 | `awaitable_frame::resume()` | `awaiting_.resume()` |
| 源码入口 | `awaitable.hpp`、`impl/awaitable.hpp`、`impl/co_spawn.hpp` | `include/sasio/awaitable.hpp`、`co_spawn.hpp` |

规模差距的根源在第 4 行。asio 的 `co_spawn(ctx, task, token)` 里，token 可以是 `use_awaitable`、`use_future`、detached……「结果怎么交回」由 token 决定，于是完成通知必须经过 `async_result` 的特化体系（第 13 章的主题）。sasio 砍掉 token，完成通知就直接写在 promise 的析构里——一句话的事。代价也如实登记：不能 `use_future`、不能 detach、每个入口一个自由函数适配器（`co_spawn.hpp` 里的 `async_wait` / `async_read_some` 等）。

还有一处值得对照的机理差异：asio 的 `awaitable_frame_base` 继承 `awaitable_launch_context`，并自定义 `operator new` 做**帧内存的回收复用**；co_spawn 的 work 计数挂在 `co_spawn_state` 的 work guard 上（`impl/co_spawn.hpp` 的 `co_spawn_work_guard`），不在帧里。sasio 的协程帧是「裸」的：内存交给编译器的协程框架，work 依赖**底层操作**（timer、socket 的挂起登记）。效果相同（协程挂着时 `run()` 不退出），机理不同——asio 把帧当资源池管理，sasio 把帧当普通对象。

## 练习

**实验**：写一个**递归的**协程链，验证 symmetric transfer 不吃栈。注意「顺序 await 一千次」不是嵌套——任意时刻只有一层在等，栈深不深跟它无关；要造出真正的深链，得让协程 await 协程 await 协程：

```cpp
sasio::awaitable<int> chain(sasio::steady_timer& t, int depth)
{
  if (depth == 0)
  {
    co_await sasio::async_wait(t);            // the only suspension, at the bottom
    co_return 0;
  }
  co_return co_await chain(t, depth - 1);     // recursion: 1000 frames alive at once
}

// co_spawn(ioc, chain(timer, 1000), [&](std::exception_ptr e, int r) { ... });

```

`chain(1000)` 会同时挂着 1000 层协程帧（每层等下一层）；注意 1000 个挂起点里**只有最深处那一个是定时器**，其余 999 层等的都是「子协程」。定时器到期后，完成沿 1000 层 `co_return` 逐层上交——每一跳都是 symmetric transfer，调试器里看调用栈：栈深不随层数增长（对照普通递归函数：1000 层就是 1000 个栈帧）。跑通后把 `chain` 的 `final_suspend` 改成 `std::suspend_always{}`（挂起后没人恢复它）再跑——观察现象：链卡在某一层不动了，`run()` 因为定时器链断了而提前停止。你刚看到的就是不变量 2 的反面教材：**没人收的帧就是泄漏**，asio 的 awaitable_frame 用帧回收池兜底，sasio 用「顶层必销毁 + 嵌套必被 await」的约定兜底。

**bug 狩猎**：下面这段代码编译失败，错误指向 `co_await` 那行：

```cpp
auto task = return_after_wait(timer);
co_await task;        // ← compile error

```

两个问题：错误信息里的哪个词提示了原因？正确的写法是什么？更进一步——为什么设计成 move-only 比「允许拷贝」更安全？提示：拷贝出来的两个 awaitable 若都被 co_spawn，谁负责销毁协程帧？

---

回调风格与协程风格至此都齐了。但第 12 章之前，本书反复提到一层「被砍掉的组合层」：executor 与 completion token。它们凭什么值 16 万行里的几万行？先把 executor 拆给你看。
