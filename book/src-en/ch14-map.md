<!-- translated-from: book/src/ch14-map.md sha256:3003a21b2560 -->

# A Reading Map for Real Asio

Through chapter 13, both cut layers — executors and completion tokens — are
dissected, and sasio's nine chapters (ch3–11) have walked the engine and the
composition layer once each. What remains is the book's opening promise:
with all this understanding, how do you read the 169,063 lines of real asio?

This chapter is the one exception to the six-part skeleton and uses three sections:
the source tree's **layering conventions** (knowing the layout saves half
the wandering), a **six-week reading plan** (clear weekly targets,
verifiable), and a **symbol mapping list** — where every sasio concept lands
in asio. The documented version is 1.38.2.

## Layering Conventions: The Directory Is the Manual

`include/asio` has four conventions; follow them and finding files halves
the wandering:

1. **`asio/xxx.hpp` is the facade; `asio/impl/xxx.hpp` is the
   implementation**. `io_context.hpp` at 1,384 lines looks fearsome, but
   most of it is doc comments and `#include`s; the real logic lives in
   `detail/impl/scheduler.ipp` (745 lines). Seeing `impl/` means "the
   implementation lives here";
2. **`detail/` is the engine; `asio/` is the API**. Code under `detail/` may
   be refactored, renamed, and platform-branched freely without touching the
   public API; every header under `asio/` is a promise. The scheduler,
   reactor, timer_queue, and reactive_socket_*_op that chapters 3–11
   compared all live in `detail/`;
3. **`*_fwd.hpp` declares only**. Forward declarations are used heavily to
   break header dependencies; when an "incomplete type" blocks your reading,
   look in `*_fwd.hpp` for where the real declaration lives;
4. **Files split by platform**. The same-named `epoll_reactor` /
   `kqueue_reactor` / `select_reactor` / `dev_poll_reactor` /
   `win_iocp_io_context` / `io_uring_service` are one concept's backends on
   different platforms; reading one teaches you all six — they implement the
   same interface (reactor semantics or IOCP semantics).

Two more "non-file organizations": the **operation object**
(`detail/scheduler_operation`) is asio's universal envelope — handler,
allocator, work unit, and cancellation state bundled into one heap object,
queued in op_queues, executed and destroyed by the scheduler. sasio
substituted member slots + `std::function`, with the equivalence and the
cost covered in chapters 7 and 8. **handler tracking**
(`detail/handler_tracking.hpp`, 90 lines) is a debugging tool: define
`ASIO_ENABLE_HANDLER_TRACKING` and every initiation and completion logs a
line (`@asio|timestamp|action`) — for "did this handler ever run" questions
it beats a debugger.

## The Six-Week Reading Plan

Two to four files and four to eight hours a week. Each week has a
completion criterion; if you miss it, stay on that week — do not rush.

**Week 1: io_context and the scheduler** (with chapters 3–4)

- Read: `asio/io_context.hpp` (1,384 lines, skipping doc comments),
  `detail/impl/scheduler.ipp` (745 lines; focus on `do_run_one`);
- Criterion: you can answer "two threads calling `run()` — who executes
  which handler, and where do queued handlers go after `stop()`";
- Self-test: how does `outstanding_work_` correspond to sasio's four
  conditions? Which lines of asio correspond to sasio's
  `maybe_stop_if_idle`?

**Week 2: the reactor (read one backend thoroughly)** (with chapter 5)

- Read: `detail/epoll_reactor.hpp` (314 lines) +
  `detail/impl/epoll_reactor.ipp` (838 lines; focus on `start_op` / `run` /
  `wait`); outside Linux, read `select_reactor` (the closest structure to
  sasio's snapshot model);
- Criterion: you can explain "why a new registration does (or does not need)
  to wake the waiting thread" and "what a speculative read is" (asio tries
  the I/O directly on readiness, and re-registers on failure — more
  aggressive than sasio's "move out and re-register");
- Self-test: what does sasio's `if (first) interrupt()` correspond to in
  epoll_reactor? (Answer: epoll's kernel-side registration makes new
  descriptors naturally visible — the counterpart is `epoll_ctl` plus the
  op_queue enqueue, not a wake-up.)

**Week 3: socket operations** (with chapter 7)

- Read: `detail/reactive_socket_recv_op.hpp` (218 lines) — a complete
  operation-object specimen; `detail/socket_ops.hpp` (428 lines) +
  `detail/impl/socket_ops.ipp` (4,712 lines; consult on demand, do not read
  straight through);
- Criterion: you can draw the object graph of `async_read_some` from
  initiation to completion (socket → service → reactor op → scheduler
  queue);
- Self-test: what else lives in an operation object besides the handler?
  (allocator, work, cancellation slot — which of sasio's slots lack them,
  and what breaks?)

**Week 4: composed operations and completion conditions** (with chapter 8)

- Read: `asio/read.hpp` (1,553 lines — how the initiating function is
  written) + `asio/impl/read.hpp` (850 lines, `detail::read_op`);
  `write.hpp` / `impl/write.hpp` symmetric (1,532 / 743 lines);
- Criterion: you can explain the difference between
  `transfer_exactly(n)` and `transfer_at_least(n)`, and where in the op the
  completion condition is consulted;
- Self-test: sasio's exercise had you write `transfer_at_least(n)` — how
  does asio's `base_from_completion_cond` make it a policy?

**Week 5: strand and executors** (with chapters 10 and 12)

- Read: `asio/strand.hpp` (579 lines) + `detail/impl/strand_service.ipp`
  (211 lines — short enough to read whole); `asio/associated_executor.hpp`;
- Criterion: you can answer "on which thread does a strand handler run,"
  "what is dispatch's inline condition," and how sasio's
  `can_dispatch() && !running` maps to it;
- Self-test: what is asio's counterpart of sasio's
  `thread_local current_strand_impl`? (the executor's call-stack comparison —
  `running_in_this_thread` goes through the associated executor.)

**Week 6: coroutines** (with chapter 11)

- Read: `asio/awaitable.hpp` (163 lines) + `asio/impl/awaitable.hpp`
  (1,121 lines; focus on `awaitable_frame`'s `resume` / `destroy`);
  `asio/co_spawn.hpp` (525 lines) + `impl/co_spawn.hpp` (449 lines);
- Criterion: you can answer "who owns the coroutine frame's memory (custom
  `operator new`), who owns co_spawn's work (the `co_spawn_state` guard),
  and how `use_awaitable`'s token becomes a completion handler (the
  `async_result` specialization — chapter 13)"; 
- Self-test: sasio's `~promise_type` calls completion — where is asio's
  corresponding action?

Beyond six weeks, extensions as needed: `detail/handler_tracking.hpp` (90
lines, debugging), `io_uring_service` (the newest backend),
`asio/execution/*.hpp` (the property framework in full — chapter 12's deep
water), `src/examples/` (the official examples, runnable, one folder per
topic).

## The Symbol Mapping List

After sasio, these concepts should be "recognized at a glance" in asio:

| sasio concept | asio landing (1.38.2) |
| --- | --- |
| `execution_context` / `service` | `asio/execution_context.hpp` + `detail/service_registry.hpp` |
| `io_context::work` | `executor_work_guard` / `make_work_guard` |
| `scheduler::run_one_impl` | `detail/impl/scheduler.ipp` `do_run_one()` |
| `handler_work` | `work_cleanup` (work release at operation completion) |
| `reactor::start_read` / `if (first)` | `select_reactor::start_unready_op`'s `if (first) interrupter_.interrupt()` |
| `timer_queue::add` returning earliest | `select_reactor::schedule_timer`'s `if (earliest)` |
| `timer_queue::pop_at` | `detail/timer_queue.hpp`'s heap deletion (no such bug; asio uses per_timer_data lists) |
| `start_read_some` / `do_read` | `basic_stream_socket::async_read_some` → `detail/reactive_socket_recv_op.hpp` |
| `read_op` / `write_op` relay | `impl/read.hpp` / `impl/write.hpp`'s `detail::read_op` / `write_op` |
| `can_dispatch() && !running` | `strand_service::do_dispatch`'s `can_dispatch() && !locked_` |
| the `drain()` loop | `strand_impl::run()` |
| `awaitable<T>`'s `promise_type` | `impl/awaitable.hpp`'s `awaitable_frame` |
| `co_spawn`'s `top_level` + completion | `impl/co_spawn.hpp`'s `co_spawn_state` |
| the resolver's short-lived thread | `detail/resolver_thread_pool.hpp` (a resident pool) |

This list is also the book's acceptance test: **for every row, you can find
it in the asio sources without opening this book** — at that point, the book
has done its job.

## Closing

sasio is not a shrunken asio; it is its **explanation vehicle**: the engine
chapters (ch4–6) show why asio can neither stop nor spin, the I/O chapters
(ch7–9) show why initiation and completion split into two phases, the strand
and coroutine chapters (ch10–11) show how the two great pains of callbacks
are solved, and Part V (ch12–14) hands back, piece by piece, the
generalization machinery that was cut.

After reading, only two things are worth returning to: the asio source
itself (the single source of truth), and the predictions you wrote in the
exercises — the wrong ones especially. The hard part of async programming
was never the API; it is "what happens between two things happening at the
same time." Everything this book taught was aimed at making that "between"
clearer.
