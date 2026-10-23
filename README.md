# asio-simplified

**English** · [简体中文](README.zh-CN.md) · [日本語](README.ja.md) · [한국어](README.ko.md)

[![CI](https://github.com/yanxijian/asio-simplified/actions/workflows/ci.yml/badge.svg)](https://github.com/yanxijian/asio-simplified/actions/workflows/ci.yml)
[![License: BSL-1.0](https://img.shields.io/badge/License-BSL--1.0-blue.svg)](LICENSE_1_0.txt)
[![C++](https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-blue.svg)]()
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()
[![Status: educational](https://img.shields.io/badge/Status-educational-orange.svg)]()

A small, readable subset of [Asio](https://github.com/chriskohlhoff/asio) for studying how
asynchronous I/O libraries work.

> **Unofficial.** This project is not affiliated with, endorsed by, or sponsored by the Asio
> project or its author. For production use, use Asio itself.

## Why

Asio 1.38.2 is 588 headers and about 164,000 lines: eight platform backends, an executor
framework, completion-token dispatch and C++03 compatibility layers. The design is excellent,
but it is hard to see the design through the machinery.

asio-simplified keeps the model and drops the machinery: Windows only, `WSAPoll`, about 4,100
lines, no third-party dependencies, and file/type names that mirror Asio so the two can be read
side by side. The C++ namespace and CMake target are `sasio`.

## What is inside

| Chapter | What it adds | Asio counterpart |
| --- | --- | --- |
| 1 | `execution_context`, `io_context`, `scheduler`, `reactor`, `timer_queue`, `steady_timer` | `detail/scheduler`, `detail/select_reactor`, `detail/timer_queue` |
| 2 | `buffer`, `detail/socket_ops`, `ip/tcp` (`socket`, `acceptor`) | `basic_socket*`, `detail/socket_ops` |
| 3 | `strand` (serialised handlers) | `strand`, `detail/strand_service` |
| 4 | composed `async_read` / `async_write` | `read.hpp`, `write.hpp`, `detail/read_op` |
| 5 | `ip/udp`, `ip/resolver` | `ip/udp.hpp`, `ip/basic_resolver.hpp` |
| 6 | C++20 `awaitable`, `co_spawn` | `awaitable.hpp`, `co_spawn.hpp`, `use_awaitable` |

## Core ideas

- **`io_context` is a service container.** The scheduler, reactor, timer queue, strand and
  socket services live in an `execution_context` and are created on demand.
- **Handlers always go through a queue.** `post()` only enqueues; `run()` drains. Each queued
  handler holds one work unit, and the context stops itself when the count reaches zero, so a
  second `run()` needs `restart()`.
- **The reactor fakes a proactor.** `WSAPoll` reports readiness; the socket operation then
  performs `recv`/`send`/`accept`/`connect` and posts its completion. Poll sets are snapshots,
  so new interest and earlier timers must interrupt the wait — otherwise another thread's
  operation would sleep until the next `post`.
- **One operation per direction.** A socket stores one read and one write slot; a second
  operation in the same direction fails with `already_started`.
- **strand, composed operations, coroutines.** A strand is a queue plus a running flag; a composed
  operation is a `shared_ptr` state machine over `*_some` calls; an `awaitable` resumes its
  awaiting coroutine through symmetric transfer.

## Build and test

Requirements: Windows, MSVC (or any C++17 compiler), CMake 3.20+ and Ninja. The coroutine test
is compiled as C++20; the library itself is C++17.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Examples:

```bat
build\ex01_post.exe
build\ex02_timer.exe
build\ex03_echo_server.exe 5555
build\ex04_bind_executor.exe
```

## Tests

Nine test executables. Where Asio has a suitable unit test, the test is adapted from it and the
original copyright header is kept: `io_context`, `deadline_timer` (as `steady_timer`), `strand`,
`read`, `write` and the `no_delay` part of `ip/tcp`. Asio has no runtime unit test for raw socket
I/O, UDP or coroutines, so those cases are derived from its examples and documented behaviour and
say so in the file header. Everything that does not fit the simplified API is marked `OMITTED`.

Debug CRT assertions are converted into immediate failures, so a broken invariant can never be
dismissed with an "Ignore" button and still report success.

## Deliberate limits

- Windows and `WSAPoll` only; no IOCP backend (the optional appendix was not built).
- IPv4 TCP and UDP only; no IPv6, no SSL, no local sockets or serial ports.
- No executor framework, no completion tokens, no `any_io_executor`.
- One outstanding operation per direction, one buffer per operation, no buffer sequences.
- The resolver starts one short-lived thread per query; `io_context` must outlive outstanding
  queries.
- Objects are not thread-safe: do not `close()` a socket from another thread while it is waiting.
- Not a drop-in replacement for Asio, and not intended for production.

Executor association and completion tokens are deliberately absent.
[Executor and completion tokens](docs/en/executor-and-tokens.md) explains what
they solve, what their absence costs, and how `examples/ex04_bind_executor.cpp`
approximates `bind_executor` with a `strand`.

## Reading order

1. `include/sasio/execution_context.hpp` — why `io_context` is so thin.
2. `src/scheduler.cpp`, `run_one_impl()` — the queue/work/timeout loop.
3. `src/reactor.cpp` — `WSAPoll`, the descriptor table and the interrupter.
4. `src/timer_queue.cpp`, `src/steady_timer.cpp` — the heap and the wake-up rule.
5. `include/sasio/ip/tcp.hpp`, `src/ip_tcp.cpp` — initiate, register, complete.
6. `src/strand.cpp`, `include/sasio/read.hpp`, `include/sasio/awaitable.hpp` — serialisation,
   composition and coroutines.

## License and attribution

Distributed under the [Boost Software License 1.0](LICENSE_1_0.txt). Test material adapted from
Asio is copyright (c) 2003-2026 Christopher M. Kohlhoff and is used under the same license; see
[NOTICE](NOTICE) for details.
