# 为什么先读 4200 行的版本

打开 [chriskohlhoff/asio](https://github.com/chriskohlhoff/asio) 的源码，1.38.2 版本：`include/asio` 目录下 600 个 `.hpp` 头文件、169,063 行（把 `.ipp` 实现文件也算上，约 19.7 万行）。在这之前你大概率只用过它的 API：

```cpp
asio::io_context io_context;
asio::ip::tcp::socket socket(io_context);
socket.async_read_some(buffer, handler);
io_context.run();

```

四行代码，直觉上并不难。可当你想回答「`run()` 到底在跑什么」「handler 是谁、在哪个线程、什么时候被调用」，顺着 `async_read_some` 点进去，第一屏是 `basic_stream_socket` 的模板声明——协议、executor 两层参数；往下是实现，先经过 `detail/reactive_socket_recv_op` 的 operation 对象，再经过 associated executor 的查找链，旁边还站着 6 套平台 I/O 后端（epoll、kqueue、select、dev_poll、IOCP、io_uring，另有 `null_reactor`、`spin_reactor` 两个非平台桩）等你分辨哪个在生效。

问题不在 asio 写得不好——它是工业级的库，那些层次各有用处。问题在于**你想理解的「异步模型」和支撑它落地的「通用化机械」交织在一起**：模型本身大概两三千行就讲完了，剩下的是让模型能在六种平台上跑、能接任意 completion token 的机械。

所以这本书的做法是反过来的：先把模型放进一个 4200 行的小实现里读透，再带着模型去读 asio。这个小实现叫 **asio-simplified**（下文简称 sasio），就是本书附带的本仓库。

## 本书的方法

sasio 的取舍只有一句话：**保留引擎，砍掉组合层**。它有 `io_context`、handler 队列、reactor、定时器堆、strand、组合操作、UDP、resolver 和 C++20 协程——asio 的全部主干；它没有 executor 体系、没有 completion token、没有平台分支（只做 Windows + WSAPoll）、没有 IPv6。砍掉的每一处，都有一份文档说明「为什么砍」和「去 asio 哪里找」，全书各章末尾的对照表就是这些说明的汇总。

读它的方法，也是读任何源码的方法，三步：

1. **trace 一个操作**：挑一条完整的调用链（比如 `async_read_some` 从发起到 handler 执行），从头走到尾，记下每一步谁调用谁、数据放在哪；
2. **改代码验证**：对走链路时产生的猜想，改一行代码、跑一遍测试来证实或推翻；sasio 够小，改起来没有心理负担；
3. **回 asio 找对应物**：在 sasio 里理解了模型，再翻 asio 的同名文件——类名、分层都是对齐的，你会认得出每一个概念，只是套上了模板。

第 3 步之所以可行，是因为 sasio 不是「类似 asio 的东西」，而是按 asio 的**设计**写的：`io_context` 一样是服务容器，`scheduler` 一样用 work 计数加三类待办来源决定停止，reactor 一样靠 interrupter 唤醒。差异都有意为之，且每一处都登记在册。

全书 14 章。除本章按 Part 0 变体、第 12–14 章按 Part V 变体外，每章固定六段：先讲「没有它会怎样」，再给数据结构和不超过三条不变量，然后 trace 一个操作的生命周期，精读一段最关键的代码，对照 asio，最后留一个实验和一个 bug 狩猎。你不需要按顺序背这些段落——它们是同一种思考过程的重复演练。

## 全书的路线

sasio 的组件依赖关系是一条线，本书就顺着这条线走：

```text
io_context ── service registry (ch3)
    │
    ├─ scheduler ──── handler queue, work count (ch4)
    │
    ├─ reactor ────── WSAPoll, descriptor table, wake-ups (ch5)
    │
    ├─ timer_queue ── min-heap, timers (ch6)
    │
    ├─ socket/acceptor ── initiate and complete, two phases (ch7)
    │
    ├─ composed operations ── async_read / async_write (ch8)
    │
    ├─ UDP / resolver ── two reuses of one engine (ch9)
    │
    ├─ strand ─────── serialisation (ch10)
    │
    └─ awaitable ───── C++20 coroutines (ch11)

```

前两章不在依赖链上，但缺了它们读链路会不完整：第 2 章立起 Reactor 与 Proactor 的模型，并给出全书反复引用的三条引擎契约。第 12 到 14 章回到 asio：executor 与 completion token 两层被砍掉的机械怎么工作、真 asio 怎么读。

每个组件都带来自家测试套件的用例——sasio 的 9 个测试文件移植自 `asio/src/tests/unit/`，断言和注释尽量保持原样。这意味着你在 sasio 上验证的行为，就是 asio 的行为，不是仿制品的近似。

## 第一段代码

先看 sasio 最小的完整程序——`examples/ex01_post.cpp` 的核心几行：

```cpp
sasio::io_context ctx;

ctx.post([] { std::puts("handler 1"); });
ctx.post([] { std::puts("handler 2"); });
ctx.post([] { std::puts("handler 3"); });

ctx.run();

```

`post` 把三个 handler 排进队列，然后返回。此时一个 handler 都没执行——post 的承诺只是「排队」，不是「运行」。`run()` 才是执行者：它从队列里逐个取出 handler 运行，直到队列空了、定时器没了、没有描述符登记了——没有活干了，它返回。程序输出三行，退出。

三个行为值得你停在原地多看一眼：

- `post` 返回时，handler **一次都没跑**。发起和执行是分开的，这根分界线贯穿全书；
- `run()` 返回的前提是**没有任何待办**——不只是队列空，还包括挂着的定时器、登记的描述符（第 4 章细讲这个「没有活干」的判定）；
- 一旦 `run()` 因为没活干而返回，io_context 进入 stopped 状态，想再跑必须 `restart()`。这是 asio 的行为，sasio 照搬。

几行代码，三个要素全部出现：**队列**（handler 在哪排）、**work**（凭什么继续跑）、**停止**（什么时候算完）。它们是全书前半的核心要素，第 3、4 章各领一个。

## sasio 和 asio 的体量对照

| | sasio | asio 1.38.2 |
| --- | --- | --- |
| 头文件 | 18 个 | 600 个（`include/asio` 下 `.hpp`） |
| 行数 | 4,207（实测；`include/sasio` + `src` 的 `.hpp`/`.cpp`） | 169,063（不含 `.ipp`；含则约 19.7 万） |
| I/O 后端 | 1（WSAPoll） | 6（epoll / kqueue / select / dev_poll / IOCP / io_uring） |
| executor / completion token | 无 | 有，贯穿全部 API |
| 测试 | 9 个，移植自 asio 测试套件 | 数百个 |

这表不是「小的比大的好」，而是**读的顺序**：4200 行里没有一行是你可以跳过的机械，169,063 行里大部分是。先把前者读透，后者就变成了「认出熟悉的模型 + 理解每层机械为什么存在」两件事，难度骤降。

## 练习

**实验**：从零构建 sasio 并跑通全部测试。以下命令在仓库根目录执行（Windows + MSVC 环境）：

```bat
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

```

9 个测试应当全绿，总耗时约 10 秒。如果 `Ninja` 不在，用默认生成器即可，`cmake --build build` 会自动调 MSBuild。

**bug 狩猎**：打开 `tests/asio/io_context.cpp`，找到 `io_context_test` 里处理异常的那段：队列里交替排着抛 `int` 的 handler 和正常递增的 handler，外面用 try/catch 循环重跑 `run()`。先预测三个问题，再运行验证：

1. 第一个 handler 抛出后，**排在它后面**的正常 handler 这次还会执行吗？
2. 异常飞出 `run()` 之后，`stopped()` 是 true 还是 false？
3. 循环为什么需要跑三轮才 break？

把预测写下来。第 4 章精读 `run_one_impl` 时回来对答案——那时你会发现自己已经能从行为反推出实现了。

---

模型部分从下一章开始。先回答一个看起来不像问题的题：「异步」到底异步在哪？`recv` 返回就有结果的那个「终点」，是怎么被挪走的？
