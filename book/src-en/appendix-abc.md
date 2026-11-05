<!-- translated-from: book/src/appendix-abc.md sha256:9634cbc50de5 -->

# Appendix A/B/C: Glossary · The Asio Tree · The Test Suite

## Appendix A: Glossary

In order of appearance, all keeping their English names (the convention from
chapter 1), one sentence each:

| Term | One-sentence definition |
| --- | --- |
| synchronous call | return means the result is in hand; a return means the call has finished |
| reactor | the kernel only reports "who is ready"; the framework does the I/O (select/poll/epoll/WSAPoll) |
| proactor | the kernel performs the I/O; the callback receives the result (Windows IOCP) |
| handler | the async operation's completion function, carrying the result or error |
| initiating function | e.g. `async_read_some` — registers only, never executes |
| `io_context` | the service container plus the host of the `run()` loop |
| service | an engine part hung on the context (scheduler, reactor, timer queue) |
| work (unit) | the "something is pending" counting unit; zero count and three empty sources means stop |
| work guard | `io_context::work` / `executor_work_guard` — a work unit held on purpose |
| op_queue / operation | asio's universal envelope: handler + allocator + work + cancellation, on the heap |
| completion signature | e.g. `void(error_code, size_t)` — decides the handler's shape |
| executor | the first-class abstraction of "where and how a handler runs" (ch12) |
| associated executor | the handler's own executor property, falling back through a lookup chain |
| completion token | the compile-time policy for "how the result comes back" (ch13) |
| `async_result` | the token's specialization table: return type and handler shape |
| strand | the serialisation guarantee: same-strand handlers never concur, and keep order |
| drain | the strand's "take → run → take again" loop |
| interrupter | the wake-up pipe: write one byte and the poller wakes |
| spurious readiness | poll said readable but `recv` returned `would_block` |
| symmetric transfer | control passing between coroutines without stacking frames |
| operation aborted | the cancelled completion's error code; cancelling ≠ no notification |
| proactor simulation | after readiness the framework performs the I/O, then wraps the result as a completion |

## Appendix B: The Asio Source Tree

`include/asio` (1.38.2, 600 `.hpp`, 169,063 lines), top-down in four layers —
drill as needed:

```text
asio/
├─ *.hpp                 public API facades (io_context, socket, read, strand…)
├─ impl/*.hpp            those facades' implementations (read.hpp → impl/read.hpp)
├─ ip/、ssl/、ts/        protocols and extensions (ip/tcp, udp, resolver…)
├─ execution/*.hpp       the executor property framework (blocking, outstanding_work…)
└─ detail/               the engine interior (refactor zone; public API ignores its shape)
   ├─ scheduler.ipp      handler queue and run loop (745 lines)
   ├─ *_reactor*         platform I/O backends (epoll/kqueue/select/dev_poll/
   │                     win_iocp/io_uring + null/spin stubs)
   ├─ reactive_socket_*_op  socket operation objects (recv_op 218 lines and up)
   ├─ socket_ops.ipp     Winsock/BSD thin wrappers (4,712 lines)
   ├─ timer_queue*       the timer heap
   ├─ strand_service     strand draining (211 lines)
   └─ handler_tracking   initiation/completion logs (90 lines, ASIO_ENABLE_HANDLER_TRACKING)
```

Three file-finding heuristics: ① lost in `asio/xxx.hpp`? go to
`asio/impl/xxx.hpp`; ② engine behavior lives in `detail/impl/*.ipp`; ③ the
same-named `*_reactor*` files — read one per platform, the rest follow.

## Appendix C: The Test Suite

sasio's 9 test files (`tests/asio/`) are **ported from asio's own tests**
(1.38.2's `src/tests/unit/`), with file headers noting provenance and cuts.
This is not ceremony — chapter 6's heap out-of-bounds and chapter 10's
dispatch deviation were both caught by these tests.

| Test file | asio source | Covers |
| --- | --- | --- |
| `io_context.cpp` | `unit/io_context.cpp` | queue, work, stop/restart, exception propagation |
| `steady_timer.cpp` | `unit/deadline_timer.cpp` | expiry, chaining, cancel, cross-thread wake-up |
| `ip_tcp.cpp` | `unit/ip/tcp.cpp` + the official echo example | no_delay, echo, cancel, cross-thread wake-up |
| `strand.cpp` | `unit/strand.cpp` | nested dispatch, exception recovery, multi-thread serialisation, dispatch outside run |
| `read.cpp` / `write.cpp` | `unit/read.cpp` / `write.cpp` | partial read/write relay |
| `ip_udp.cpp` | `unit/ip/udp.cpp` (rewritten as an echo case) | send_to / receive_from |
| `resolver.cpp` | `unit/ip/basic_resolver.cpp` (assertion style reference) | sync resolution, localhost loopback |
| `coroutine.cpp` | `unit/co_spawn.cpp` / `awaitable.cpp` (model reference) | co_spawn result/exception, timer suspension, socket session |

Two defenses in the harness (`unit_test.hpp`) worth knowing:

1. **Assertions do not pop dialogs**: the Debug CRT's assertion normally
   offers Abort/Retry/Ignore, and Ignore lets an out-of-bounds program keep
   running and even print "passed". The harness installs `_CrtSetReportHook2`
   to turn assertions into stderr + `_Exit(3)` — immediate death, CTest
   red;
2. **Error counter**: `ASIO_CHECK` counts without interrupting, and the
   suite returns nonzero by count — one run shows all failures, not just
   the first.

Run the whole suite: `cmake --build build && ctest --test-dir build
--output-on-failure` (9 tests, about 10 seconds). The two cross-thread
wake-up regression tests from chapters 5 and 6 are the model of
discriminating test design: the fix missing means certain failure (and no
hang).
