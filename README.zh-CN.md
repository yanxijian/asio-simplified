# asio-simplified

**简体中文** · [English](README.md) · [日本語](README.ja.md) · [한국어](README.ko.md)

[![CI](https://github.com/yanxijian/asio-simplified/actions/workflows/ci.yml/badge.svg)](https://github.com/yanxijian/asio-simplified/actions/workflows/ci.yml)
[![License: BSL-1.0](https://img.shields.io/badge/License-BSL--1.0-blue.svg)](LICENSE_1_0.txt)
[![C++](https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-blue.svg)]()
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()
[![Status: educational](https://img.shields.io/badge/Status-educational-orange.svg)]()

<!-- translated-from: README.md sha256:664a0fbdd14d -->

[Asio](https://github.com/chriskohlhoff/asio) 的一个小而可读的子集，用来学习异步 I/O 库是如何工作的。

> **非官方项目。** 本项目与 Asio 项目及其作者没有隶属、背书或赞助关系。生产环境请直接使用 Asio。

## 为什么

Asio 1.38.2 有 588 个头文件、约 16.4 万行：8 套平台后端、executor 框架、completion token 分派，
以及 C++03 兼容层。设计很好，但机械结构把设计遮住了。

asio-simplified 保留模型、去掉机械结构：只做 Windows、只用 `WSAPoll`、约 4100 行、无第三方依赖，
文件名和类型名与 Asio 对齐，可以左右对照着读。C++ 名字空间与 CMake target 都是 `sasio`。

## 内容

| 章 | 新增内容 | Asio 对照 |
| --- | --- | --- |
| 1 | `execution_context`、`io_context`、`scheduler`、`reactor`、`timer_queue`、`steady_timer` | `detail/scheduler`、`detail/select_reactor`、`detail/timer_queue` |
| 2 | `buffer`、`detail/socket_ops`、`ip/tcp`（`socket`、`acceptor`） | `basic_socket*`、`detail/socket_ops` |
| 3 | `strand`（handler 串行化） | `strand`、`detail/strand_service` |
| 4 | 组合操作 `async_read` / `async_write` | `read.hpp`、`write.hpp`、`detail/read_op` |
| 5 | `ip/udp`、`ip/resolver` | `ip/udp.hpp`、`ip/basic_resolver.hpp` |
| 6 | C++20 `awaitable`、`co_spawn` | `awaitable.hpp`、`co_spawn.hpp`、`use_awaitable` |

## 核心思想

- **`io_context` 是一个服务容器。** 调度器、reactor、定时器队列、strand 和 socket 服务都住在
  `execution_context` 里，按需创建。
- **handler 永远走队列。** `post()` 只入队，`run()` 才排空。每个入队 handler 占一个 work 单位，
  计数归零时 context 自动停止，所以再次 `run()` 之前必须 `restart()`。
- **reactor 把就绪伪装成完成。** `WSAPoll` 报告就绪后，socket 操作才执行
  `recv`/`send`/`accept`/`connect` 并投递完成 handler。poll 集合是快照，所以新的兴趣和更早的
  定时器必须打断等待——否则另一个线程发起的操作会一直睡到下一次 `post`。
- **每个方向一个未完成操作。** socket 只有一个读槽和一个写槽；同方向再发起会得到
  `already_started`。
- **strand、组合操作、协程。** strand 是队列加运行标志；组合操作是用 `shared_ptr` 状态串起多次
  `*_some` 的状态机；`awaitable` 通过 symmetric transfer 恢复等待它的协程。

## 构建与测试

需要 Windows、MSVC（或任意 C++17 编译器）、CMake 3.20+ 和 Ninja。协程测试按 C++20 编译，
库本身是 C++17。

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

示例：

```bat
build\ex01_post.exe
build\ex02_timer.exe
build\ex03_echo_server.exe 5555
```

## 测试

共 9 个测试可执行文件。凡是 Asio 有合适单测的，都从它裁剪而来并保留原始版权头：`io_context`、
`deadline_timer`（改为 `steady_timer`）、`strand`、`read`、`write`，以及 `ip/tcp` 的 `no_delay` 部分。
Asio 没有针对原始 socket I/O、UDP 和协程的运行时单测，这些用例改由其示例和文档行为构造，并在
文件头注明。不适配简化 API 的部分都标了 `OMITTED`。

Debug CRT 断言被改成立即失败，坏掉的不变量不可能靠点一次“Ignore”继续跑完还报告成功。

## 有意的限制

- 只支持 Windows + `WSAPoll`；没有 IOCP 后端（可选附录未实现）。
- 只支持 IPv4 的 TCP 与 UDP；没有 IPv6、SSL、本地套接字和串口。
- 没有 executor 框架、completion token、`any_io_executor`。
- 每个方向一个未完成操作，每个操作一个 buffer，没有 buffer 序列。
- resolver 每次查询起一个短命线程；`io_context` 必须比未完成的查询活得久。
- 对象不是线程安全的：不要在另一个线程 `close()` 正在等待的 socket。
- 不是 Asio 的 drop-in 替代品，也不面向生产环境。

## 阅读顺序

1. `include/sasio/execution_context.hpp`——`io_context` 为什么这么薄。
2. `src/scheduler.cpp` 的 `run_one_impl()`——队列/work/超时循环。
3. `src/reactor.cpp`——`WSAPoll`、描述符表和 interrupter。
4. `src/timer_queue.cpp`、`src/steady_timer.cpp`——堆与唤醒规则。
5. `include/sasio/ip/tcp.hpp`、`src/ip_tcp.cpp`——发起、注册、完成。
6. `src/strand.cpp`、`include/sasio/read.hpp`、`include/sasio/awaitable.hpp`——串行化、组合与协程。

## 许可与归属

以 [Boost Software License 1.0](LICENSE_1_0.txt) 分发。改编自 Asio 的测试材料版权归
Christopher M. Kohlhoff（2003-2026），按同一许可使用；详见 [NOTICE](NOTICE)。
