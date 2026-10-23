# sasio — 用来读 asio 的简化版

`D:\Codes\asio` 是 chriskohlhoff/asio 的完整克隆（1.38.2，588 个头文件、约 16.4 万行）。
直接读它，第一道坎不是模型，而是平台分支、模板层次和 C++03 兼容代码。

`sasio` 是同一套**设计思想**的极小实现：只做 Windows、只保留主干，`include/` + `src/`
约 3000 行，并且**文件名、类名、分层都与 asio 对齐**，可以左右对照着读。

它不是 asio 的替代品，也不追求 API 兼容；它是 asio 的**伴读代码**。

## 已实现（第 1、2 章）

| sasio | 对应 asio | 说明 |
| --- | --- | --- |
| `include/sasio/execution_context.hpp` | `execution_context.hpp` + `detail/service_registry.hpp` | 服务注册表：`io_context` 只负责创建/查找服务 |
| `include/sasio/io_context.hpp` | `io_context.hpp` | 门面：`post`/`dispatch`/`run`/`poll`/`stop` + `work` 守卫 |
| `include/sasio/scheduler.hpp` | `detail/scheduler.hpp`、`detail/scheduler_operation.hpp` | handler 队列、work 计数、`run` 主循环 |
| `include/sasio/reactor.hpp` | `detail/select_reactor.hpp`、`detail/epoll_reactor.hpp` | WSAPoll 就绪等待、描述符注册表 + interrupter 唤醒 |
| `include/sasio/timer_queue.hpp` | `detail/timer_queue.hpp` | 定时器最小堆 |
| `include/sasio/steady_timer.hpp` | `steady_timer.hpp`、`basic_waitable_timer.hpp` | 定时器对象，`expires_after` / `async_wait` / `cancel` |
| `include/sasio/buffer.hpp` | `buffer.hpp` | `mutable_buffer` / `const_buffer` 与 `buffer()` 辅助函数 |
| `include/sasio/detail/socket_ops.hpp` | `detail/socket_ops.hpp`（4308 行） | Winsock 薄封装：create/bind/listen/accept/connect/recv/send/shutdown |
| `include/sasio/ip/tcp.hpp` | `ip/tcp.hpp`、`basic_stream_socket.hpp`、`basic_socket_acceptor.hpp` | IPv4 TCP：`endpoint`、`socket`、`acceptor`、`no_delay` |
| `include/sasio/strand.hpp` | `strand.hpp`、`detail/strand_service.hpp` | 串行化 handler：`post`/`dispatch`/`running_in_this_thread` |
| `include/sasio/read.hpp`、`write.hpp` | `read.hpp`、`write.hpp`、`detail/read_op.hpp` | 组合操作：把一个 buffer 读满 / 写空，内部循环 `*_some` |
| `include/sasio/error.hpp` | `error.hpp` | `error_code`、`operation_aborted`、`would_block`、`already_started`、`eof` |

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

### 4. socket 操作也是「注册 → 就绪 → 执行 → 投递完成」

`async_read_some` 不会读数据，它只做三件事：把 buffer/handler 存进 socket、向 reactor 注册
「读就绪」、返回。reactor 报告可读后，`socket::do_read()` 才真正 `recv`，然后把完成 handler
`post` 到调度器。写、accept、connect 完全同构。

这条链路和 asio 的 `reactive_socket_recv_op` 一一对应：**你写下的 `async_xxx` 只是描述
「就绪后做什么」，真正的系统调用发生在 reactor 通知之后**。它也解释了 asio 为什么要
operation 对象：注册、取消、完成都需要一个能活到事件发生时的状态载体；这里直接用
socket 成员槽位代替。

第 2 章还给 reactor 增加了描述符注册表：`start_read` / `start_write` / `stop_*` 与
`close_descriptor`。定时器仍然不经过描述符，由调度器算超时。

### 5. strand 把「多线程 run()」变成串行

`run()` 可以被多个线程同时调用，此时不同 handler 会并发执行——这正是 asio 需要 strand 的原因。

`sasio::strand` 内部是一个共享的队列 + `running` 标志：

- `post()` 入队；如果 strand 空闲，就向调度器投递一个「排空」handler；
- 排空循环每次取一个 handler 执行，执行期间新投递的 handler 只入队，由同一个循环接着跑；
- `dispatch()` 在 strand 内部时直接就地执行（与 asio 的嵌套语义一致），在外部且 strand
  空闲时也允许就地接管；
- 一个 handler 抛异常不能把 strand 永久锁住：释放 `running`，把剩余 handler 重新投递。

它保证的是 asio 的两条契约：**同一 strand 的 handler 不并发，且 `post` 的顺序不变**。

### 6. 组合操作：操作套操作

`async_read` / `async_write` 自己不做 I/O，它们反复调用 `async_read_some` / `async_write_some`，
直到 buffer 满/空、出错或对端关闭。`sasio` 用一个小状态机（`detail::read_op`、`write_op`）实现：

- 状态对象由 `shared_ptr` 持有，并被每次完成 handler 捕获，于是它恰好活到整条操作链结束；
- 部分读/写由 `transferred_` 计数推进，下一轮只传剩余区间；
- 对端提前关闭时，`read` 以 `sasio::eof` 完成——对应 asio 的 `asio::error::eof`。

这是 asio 里 `composed_op` + 关联分配器做的事情的最小版本。

## 目录

```
simplified/
  include/sasio/      公开头文件（对照 asio 的同名头）
  src/                实现
  examples/           ex01_post、ex02_timer、ex03_echo_server
  tests/asio/         移植自 asio 自带测试：unit_test.hpp、io_context、steady_timer、ip_tcp、strand、read、write
```

## 构建与运行

需要 MSVC（或任意 C++17 编译器）和 CMake ≥ 3.20：

```bat
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
build\ex01_post.exe
build\ex02_timer.exe
build\ex03_echo_server.exe 5555
```

## 测试：来自 asio 自己的测试套件

测试**不是自己写的**，而是从 `asio/src/tests/unit/` 裁剪移植：

| 文件 | 来源 | 处理 |
| --- | --- | --- |
| `unit_test.hpp` | `unit_test.hpp` | 保留 `ASIO_CHECK`/`ASIO_TEST_SUITE`/`ASIO_TEST_CASE` 宏名，改成 `sasio` 名字空间、去掉平台分支 |
| `io_context.cpp` | `io_context.cpp` | 保留 `io_context_test` 的全部断言与注释；`executor_work_guard` → `io_context::work`，`post/dispatch` 改成员调用 |
| `steady_timer.cpp` | `deadline_timer.cpp` | `deadline_timer` + `posix_time` → `steady_timer` + `std::chrono`；保留到期、链式、取消的断言 |
| `ip_tcp.cpp` | `ip/tcp.cpp` + `examples/cpp11/echo/async_tcp_echo_server.cpp` | `no_delay` 断言来自 `ip_tcp_runtime::test`；echo/cancel 用例来自示例（asio 没有这一层的运行时单测） |
| `strand.cpp` | `strand.cpp` | 保留 `strand_test` 的嵌套 dispatch、异常后仍可继续、孤儿 strand 断言；多线程串行与顺序用例按同样思路补上 |
| `read.cpp`、`write.cpp` | `read.cpp`、`write.cpp` | 保留 `test_3_arg_*_async_*` 的部分读写用例；`test_stream.hpp` 也是从这两个文件里的同名辅助类移植的 |

关于 `ip_tcp.cpp` 需要说明：asio 的 `src/tests/unit/ip/tcp.cpp` 主要是 API 面与选项的检查，
**没有覆盖真实的 accept/read/write 行为**（那部分靠 `examples/` 和 `latency`/`performance`）。
所以第 2 章的 echo 与 cancel 用例是照 `async_tcp_echo_server.cpp` 改写的，文件头写明了来源。

省略的部分都在文件里标了 `OMITTED` 并注明原因，主要是第 1 章没有的能力：

- 同步 `wait()`、`cancel_one()`、一个定时器多个挂起 wait；
- `bind_executor`、自定义 executor、allocator、completion token、timer 移动语义；
- `deadline_timer_custom_allocation_test` / `_thread_test` / `_async_result_test` / `_move_test`；
- `strand_conversion_test` / `strand_query_test` / `strand_execute_test`（需要 executor 转换、`query`/`prefer` 与 `execute`）。

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
| socket/acceptor 不是线程安全的：不要从另一个线程 `close()` 正在等待的 socket | 与 asio 的约束相同；asio 靠 operation 对象缓解，这里没有跨线程状态保护 |
| 没有 `scheduler_operation` 对象 | asio 把 handler、分配器、执行器和 work 单位绑在一个 operation 里；这里用「`std::function` + `handler_work` RAII 计数」得到同样的可观察行为 |
| 定时器不单独占 work 单位 | asio 的每个 wait op 都持有一个 work 单位；这里用「定时器堆非空」等价地阻止 `run()` 退出 |
| 超时由 scheduler 计算 | asio 把定时器队列注册给 reactor，由 reactor 算 poll 超时 |
| 一个定时器只允许一个挂起 wait | asio 允许多个；简化后不必维护 wait 列表 |
| 计时器析构时投递 `operation_aborted` | 与 asio 的取消语义一致；asio 在对象析构路径上细节更多 |
| 取消的完成 handler 通过 `post` 投递 | 与 asio 一致，保证不在 `cancel()` 内部重入 |
| 只实现 IPv4 + TCP，无 `resolver` | 第 5 章再补；需要域名时先用 `getaddrinfo` 自己解析 |
| 每个方向只允许一个未完成操作 | asio 用 operation 队列支持更多重载；这里用 `already_started` 明确拒绝 |
| `socket` 不可移动、`async_accept(socket&, handler)` | 省掉 move 语义与 asio 的 `async_accept()` 返回 socket 重载 |
| 没有组合操作 `async_read`/`async_write` | 第 4 章；示例里的写循环暂时手写 |
| socket 选项只有 `no_delay` | `reuse_address` 在 acceptor 内部固定打开；其余选项留作练习 |
| `strand` 是绑定 io_context 的对象，不是 executor 适配器 | asio 的 `strand<Executor>` 需要 executor 体系；这里保留串行语义，砍掉 `bind_executor` |
| 组合操作只接受单个 buffer | asio 支持 buffer 序列、streambuf、完成条件（`transfer_at_least` 等）；这里够用即可 |
| 没有 `async_read_until` | 它需要动态缓冲和分隔符搜索，留作练习 |
| 没有 resolver、SSL、协程 | 后续章节 |

## 阅读顺序建议

1. `execution_context.hpp`：先看服务注册表，理解 `io_context` 为什么这么薄；
2. `io_context.hpp` → `io_context.cpp`：只看转发，几乎没有逻辑；
3. `scheduler.cpp` 的 `run_one_impl`：全库最重要的 60 行；
4. `reactor.cpp`：interrupter、`WSAPoll`、描述符注册表，理解「唤醒」与「就绪」；
5. `timer_queue.cpp`：最小堆，理解「超时」；
6. `steady_timer.cpp`：一个对象如何把 wait 注册进队列、取消时如何投递完成；
7. `detail/socket_ops.hpp`：所有 Winsock 调用只出现在这一层；
8. `ip/tcp.hpp` 的 `async_read_some`：发起函数只存状态 + 注册，`do_read` 才做系统调用；
9. `src/ip_tcp.cpp` 的 `do_accept` / `do_connect`：对照 asio 的 `*_op` 状态机；
10. `strand.cpp`：一个队列 + 一个 `running` 标志如何实现 asio 的串行契约；
11. `read.hpp` / `write.hpp`：组合操作如何用 `shared_ptr` 状态串起多次 `*_some`。

## 路线图

| 章节 | 内容 | asio 对照 |
| --- | --- | --- |
| 1（完成） | io_context、scheduler、reactor、timer | `detail/scheduler`、`detail/select_reactor`、`detail/timer_queue` |
| 2（完成） | `socket`、`acceptor`、`socket_ops` | `basic_socket.hpp`、`detail/socket_ops` |
| 3（完成） | `strand` 与多线程 `run()` | `strand.hpp`、`detail/strand_service.hpp` |
| 4（完成） | 组合操作 `async_read`/`async_write` | `read.hpp`、`write.hpp`、`detail/read_op` |
| 5 | `resolver`、UDP | `ip/basic_resolver.hpp` |
| 6（可选） | C++20 `awaitable` + `co_spawn` | `awaitable.hpp`、`co_spawn.hpp` |
| 附录（可选） | 最小 IOCP 版本 | `detail/win_iocp_*` |
