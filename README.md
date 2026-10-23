# sasio — 用来读 asio 的简化版

`D:\Codes\asio` 是 chriskohlhoff/asio 的完整克隆（1.38.2，588 个头文件、约 16.4 万行）。
直接读它，第一道坎不是模型，而是平台分支、模板层次和 C++03 兼容代码。

`sasio` 是同一套**设计思想**的极小实现：只做 Windows、只保留主干，`include/` + `src/`
约 1300 行，并且**文件名、类名、分层都与 asio 对齐**，可以左右对照着读。

它不是 asio 的替代品，也不追求 API 兼容；它是 asio 的**伴读代码**。

## 已实现（第 1 章）

| sasio | 对应 asio | 说明 |
| --- | --- | --- |
| `include/sasio/execution_context.hpp` | `execution_context.hpp` + `detail/service_registry.hpp` | 服务注册表：`io_context` 只负责创建/查找服务 |
| `include/sasio/io_context.hpp` | `io_context.hpp` | 门面：`post`/`dispatch`/`run`/`poll`/`stop` + `work` 守卫 |
| `include/sasio/scheduler.hpp` | `detail/scheduler.hpp`、`detail/scheduler_operation.hpp` | handler 队列、work 计数、`run` 主循环 |
| `include/sasio/reactor.hpp` | `detail/select_reactor.hpp`、`detail/epoll_reactor.hpp` | WSAPoll 就绪等待 + interrupter 唤醒 |
| `include/sasio/timer_queue.hpp` | `detail/timer_queue.hpp` | 定时器最小堆 |
| `include/sasio/steady_timer.hpp` | `steady_timer.hpp`、`basic_waitable_timer.hpp` | 定时器对象，`expires_after` / `async_wait` / `cancel` |
| `include/sasio/error.hpp` | `error.hpp` | `error_code`、`operation_aborted` |

## 三个核心概念

### 1. io_context 只是一个服务容器

asio 里 `io_context` 本身几乎没有逻辑，逻辑都在 service 里：调度器、reactor、定时器队列、
strand 服务、各种 socket 服务。`execution_context::use_service<T>()` 按需创建并持有它们。

这样加功能不需要改 `io_context`，用户也能挂自己的服务。`sasio` 保留了这个机制，
见 `execution_context.hpp`，只有约 100 行。

### 2. handler 永远走队列

`post()` 只入队，不调用；`run()` 才调用。`dispatch()` 是唯一允许「就地执行」的入口，
且只限于「当前线程已经在这个 context 的 handler 里」——asio 用 executor 比较来判断，
这里用一个 thread-local 指针。

每个入队的 handler 在运行前占一个 work 单位，运行后释放。**work 归零时 io_context 自动停止**：
`run()` 会返回，`stopped()` 变成 true；想再用必须先 `restart()`。这正是 asio 的行为
（`scheduler::work_finished()` 里 `if (--outstanding_work_ == 0) stop();`），也是 asio 程序
不提前退出的关键——挂起的定时器或 socket 读会撑着 work，线程就不会从 `run()` 退出。

`run()` 进入时若没有任何 work，asio 会先 `stop()` 再返回 0；`stopped()` 状态下调用
`run()`/`poll()` 不会执行任何 handler。

### 3. reactor 把「就绪」伪装成「完成」

Windows 上 asio 用 IOCP（真 proactor）；POSIX 上它用 reactor 模拟 proactor：注册关心的事件，
`poll` 报告就绪后再调用完成 handler。

`sasio` 走的是后一条路，只不过在 Windows 上用 `WSAPoll` 代替 `epoll`/`select`。
`scheduler::run_one_impl` 的循环就是这套模型的全部：

```
取一个已入队的 handler -> 跑
否则：看定时器堆最早到期时间 -> 作为 poll 超时
       poll（可能被 post/stop 的 interrupter 唤醒）
       循环
```

定时器不占线程、不空转：`run()` 会一直阻塞在 `WSAPoll` 里，直到下一个定时器到期或被唤醒。
会把睡着的 poll 线程唤醒的事件有两类：handler 入队（`post`）和成为最早到期者的新定时器
（`async_wait`）；第 2 章加入描述符后，首次 `start_read`/`start_write` 也会唤醒。asio 在
`select_reactor::schedule_timer()` 与 `start_unready_op()` 里用同一招
（`if (earliest/first) interrupter_.interrupt()`）——没有这一步，多线程 `run()` 时从另一个
线程发起的操作会一直睡到下一次 `post` 才被看见。

## 目录

```
simplified/
  include/sasio/      公开头文件（对照 asio 的同名头）
  src/                实现
  examples/           ex01_post（post 链）、ex02_timer（循环定时器）
  tests/asio/         移植自 asio 自带测试（unit_test.hpp、io_context、steady_timer）
```

## 构建与运行

需要 MSVC（或任意 C++17 编译器）和 CMake ≥ 3.20：

```bat
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
build\ex01_post.exe
build\ex02_timer.exe
```

## 测试：来自 asio 自己的测试套件

测试**不是自己写的**，而是从 `asio/src/tests/unit/` 裁剪移植：

| 文件 | 来源 | 处理 |
| --- | --- | --- |
| `unit_test.hpp` | `unit_test.hpp` | 保留 `ASIO_CHECK`/`ASIO_TEST_SUITE`/`ASIO_TEST_CASE` 宏名，改成 `sasio` 名字空间、去掉平台分支 |
| `io_context.cpp` | `io_context.cpp` | 保留 `io_context_test` 的全部断言与注释；`executor_work_guard` → `io_context::work`，`post/dispatch` 改成员调用 |
| `steady_timer.cpp` | `deadline_timer.cpp` | `deadline_timer` + `posix_time` → `steady_timer` + `std::chrono`；保留到期、链式、取消的断言 |

省略的部分都在文件里标了 `OMITTED` 并注明原因，主要是第 1 章没有的能力：

- 同步 `wait()`、`cancel_one()`、一个定时器多个挂起 wait；
- `bind_executor`、自定义 executor、allocator、completion token、timer 移动语义；
- `deadline_timer_custom_allocation_test` / `_thread_test` / `_async_result_test` / `_move_test`。

另外把原来的秒级等待缩短到几百毫秒，检查结构不变。

测试 harness 还把 Debug CRT 断言改成了**立即失败**：断言触发时把表达式打到 stderr 并
`_Exit(3)`，不弹「Ignore」对话框。否则点一次 Ignore 就能让一个已经越界执行的测试继续跑完，
甚至打印 “passed”。

> 这套测试移植过来后立刻抓到一个真 bug：`timer_queue::pop_at` 在移除堆顶时会把被删元素的
> `heap_index` 写回一个越界值，导致定时器析构时 Debug STL 断言 `vector subscript out of range`。
> 我自己之前的测试只有一个定时器，堆删空时没有 swap，因此没暴露；asio 的用例里有两个定时器，直接命中。

## 与 asio 的有意差异

| 差异 | 原因 |
| --- | --- |
| 只支持 Windows + WSAPoll | 教学范围；asio 有 8 套后端 |
| 没有模板 executor / `any_io_executor` | 这层是 asio 最重的机械结构，不影响主模型 |
| handler 统一为 `std::function` | 保留「发起函数 → 操作 → 完成」主干，去掉 completion token 分派 |
| 没有 `scheduler_operation` 对象 | asio 把 handler、分配器、执行器和 work 单位绑在一个 operation 里；这里用「`std::function` + `handler_work` RAII 计数」得到同样的可观察行为 |
| 定时器不单独占 work 单位 | asio 的每个 wait op 都持有一个 work 单位；这里用「定时器堆非空」等价地阻止 `run()` 退出 |
| 超时由 scheduler 计算 | asio 把定时器队列注册给 reactor，由 reactor 算 poll 超时 |
| 一个定时器只允许一个挂起 wait | asio 允许多个；简化后不必维护 wait 列表 |
| 计时器析构时投递 `operation_aborted` | 与 asio 的取消语义一致；asio 在对象析构路径上细节更多 |
| 取消的完成 handler 通过 `post` 投递 | 与 asio 一致，保证不在 `cancel()` 内部重入 |
| 没有 `strand`、socket、resolver、SSL、协程 | 后续章节 |

## 阅读顺序建议

1. `execution_context.hpp`：先看服务注册表，理解 `io_context` 为什么这么薄；
2. `io_context.hpp` → `io_context.cpp`：只看转发，几乎没有逻辑；
3. `scheduler.cpp` 的 `run_one_impl`：全库最重要的 60 行；
4. `reactor.cpp`：interrupter 与 `WSAPoll`，理解「唤醒」；
5. `timer_queue.cpp`：最小堆，理解「超时」；
6. `steady_timer.cpp`：一个对象如何把 wait 注册进队列、取消时如何投递完成。

## 路线图

| 章节 | 内容 | asio 对照 |
| --- | --- | --- |
| 1（完成） | io_context、scheduler、reactor、timer | `detail/scheduler`、`detail/select_reactor`、`detail/timer_queue` |
| 2 | `basic_stream_socket`、`acceptor`、`socket_ops` | `basic_socket.hpp`、`detail/socket_ops`（4308 行 → 约 400 行） |
| 3 | `strand` 与多线程 `run()` | `strand.hpp`、`detail/strand_service.hpp` |
| 4 | 组合操作 `async_read`/`async_write` | `read.hpp`、`write.hpp`、`detail/read_op` |
| 5 | `resolver`、UDP | `ip/basic_resolver.hpp` |
| 6（可选） | C++20 `awaitable` + `co_spawn` | `awaitable.hpp`、`co_spawn.hpp` |
| 附录（可选） | 最小 IOCP 版本 | `detail/win_iocp_*` |
