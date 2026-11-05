<!-- translated-from: book/src/appendix-de.md sha256:770fd03bce4f -->

# Appendix D/E: Answers · The Table of Deliberate Differences

# Appendix D: Exercise Answers

The complete answers to every chapter's exercises. Do them yourself first —
the answers are not going anywhere.

## ch1 experiment: build and test

The commands are in the chapter; common failures: Ninja missing → use the
default generator (executables move to `build\Debug\`); `ws2_32` link errors
mean the compiler environment is not in a VS command prompt.

## ch1 bug hunt: the exception case's three questions

1. The throwing handler **was already dequeued**; its exception has nothing
   to do with the queue. The normal handlers remain intact and keep running
   after the restart of `run()`;
2. `stopped()` is **false**. An exception is not a stop instruction: stopping
   happens only on "all four empty" or explicit `stop()`. The queue still had
   work when the exception flew, so the flag was never set;
3. Each `run()` processed at least one handler before being interrupted; the
   three normal handlers spread over three rounds, after which the queue is
   empty, all four conditions hold, `run()` returns 0 cleanly, and the loop
   breaks.

## ch2 experiment: echo server concurrency

Both connections are served simultaneously: each socket is registered in the
reactor's descriptor table, one `WSAPoll` reports several ready at once, and
the loop handles them in turn. A connected-but-silent client produces no
readiness and costs no system call.

## ch2 bug hunt: cancel's notification timing

The handler does not run inside `cancel()`'s call stack — it runs in the run
loop after the post, with error code `operation_aborted`. If it ran
synchronously in the cancel stack and touched state the caller holds, that
is reentrancy; "cancel always posts" removes the path.

## ch3 experiment: the handler_stats service

Two contexts each carrying a `handler_stats` do not interfere: invariant 1's
full wording is "at most one per service type, per **context**" — the key is
`(context, type)`, not a global type.

## ch3 bug hunt: recursive_mutex

With a plain `std::mutex`, the deadlock happens on the **second** entry into
`use_service`: `io_context` constructs → `use_service<scheduler>()` takes the
lock → the scheduler constructs → `use_service<reactor>()` takes the same
lock again. The stack trace shows two `use_service` frames and one
`std::mutex::lock`. The recursive construction chain is the direct price of
on-demand assembly.

## ch4 experiment: the work leak

The stuck loop: `work_count_` is 1 (the guard holds it), the queue is empty
after the handler ran, the timer heap is empty, no descriptors — but
`has_work_locked()` is true because `work_count_ != 0`, so every pass of
`run_one_impl` reaches step 3, computes "work exists," and sleeps in
`reactor_.run_for(max)`, unwoken. The `reset()` version: the second
context's four conditions all hold after `reset()`, and `run()` returns
**0**.

## ch4 bug hunt: the exception's three questions

See chapter 1's answers (the same case); recap: a dequeued handler is
decoupled from the queue, an exception never sets the stop flag, and the
three normal handlers spread across three rounds.

## ch5 experiment: reproducing the first miss

With both `if (first) interrupt()` lines commented, `tcp_multithread_wakeup_
test` fails at about 5 seconds: the run thread sleeps in `WSAPoll(∞)`, the
newly registered read is not in its snapshot, and the only wake source is
the 5-second watchdog. Restored, the test passes instantly. The lesson: the
snapshot model's new waiting sources must wake the waiter.

## ch5 bug hunt: spurious readiness

Two causes: **another reader consumed first** (the same datagram taken by
another `recvfrom`, or the engine's speculative read doing I/O first), or
**a false alarm itself** (poll promises only "possibly readable"). TCP
retransmission and checksums do not make a reported readiness vanish — they
are not on the list. Without re-registering: the callback has left the table
(invariant 2), the read vanishes from the reactor's world — the buffer never
sees another readiness notification, a **silent read suspension**.

## ch6 experiment: the pop_at out-of-bounds

Reproduction points: two timers (50ms / 300ms); the earlier expiry runs
`pop_at(0)` on a heap of 2, with `index(0) != last(1)`, so the swap path
writes the removed entry's `heap_index` to 1; `pop_back` leaves
`heap_.size() == 1`, and the dirty index `1 == size()` settles. The
assertion fires **later**: both timer objects destruct, `cancel()` →
`remove()` reads the dirty index into the heap, and the Debug STL's `vector
subscript out of range` pops. The detonation is far from the crime scene —
typical of index-type bugs.

## ch6 bug hunt: remove's three failure reasons

- Index `npos`: the entry already left the heap — leaving has one road
  (`take_expired` or `pop_at`), and both arrange the handler's completion
  (success delivered by the scheduler directly, on its way);
- Index out of bounds: the entry already left the heap (same as above), or a
  dirty index (impossible after the fix; the defensive check as backstop);
- Identity mismatch (`heap_[index] != e`): the index belongs to another
  entry — in-heap swaps keep indices synchronized (`swap_at`), so it should
  not happen; kept as defense.
  In all three cases the handler's completion already has an owner; nothing
  is lost.

## ch7 experiment: two sessions interleaving

They interleave. Each session's `async_read_some` completion is an
independent queue item; when both sockets are ready at once, `WSAPoll`
reports both, `do_read` runs for each in the same thread, and the two
completion handlers enqueue and execute in turn. A single-threaded `run()`'s
concurrency model is "interleaving," not "parallelism."

## ch7 bug hunt: initiating after close

`close()` already set `handle_` to `INVALID_SOCKET`, so the new
`async_read_some` takes `start_read_some`'s `!is_open()` branch: the handler
is posted with `bad_descriptor`. Order: the `operation_aborted` was posted
first, so it is ahead of the `bad_descriptor` in the FIFO queue — **the
queue's FIFO guarantees the order**. Both notifications arrive; the
validity of each buffer is the caller's responsibility.

## ch8 experiment: the state moved into write_op

The hand-written `write_offset_`/`write_size_` are session members — one set
per session, alive as long as the session; the composed version's state is
`write_op`'s `transferred_` — one per `async_write`, alive by the
shared_ptr relay, destroyed at the chain's end. The session need not hold it
because it belongs to this one write operation alone.

## ch8 extension: transfer_at_least(n)

`read_op` / `write_op` gain a `least_` member; the completion test changes
from `transferred_ == buffer_.size()` to
`transferred_ >= least_ || transferred_ == buffer_.size()`.
`least_ = 0` versus the default, by asio's **actual order** (`impl/read.hpp`'s
`read_op`): each invocation checks the completion condition **first** (the
`check_for_completion` at the top of `operator()`); after I/O, a zero-byte
read **breaks straight to completion with eof** (the zero-byte branch after
I/O), never passing the second completion check. Therefore: with
`least_ = 0` the condition holds before initiation and the call returns a
successful 0 bytes — but **not without any I/O**: the loop still issues one
zero-length `async_read_some`, completed by the zero-byte branch, moving no
data; with EOF and `least_ > 0`, the error is eof. Putting the completion
check before the I/O is what aligns with this order.

## ch9 experiment: UDP echo and concurrency

`async_send_to` outstanding while the next `async_receive_from` is already
registered — the two directions occupy independent slots and **may
proceed concurrently**; TCP differs (a session's same direction is serial —
write finished before reading again) because UDP has no stream: receive and
send are independent, and the engine registers both directions
independently. The example's `payload` copy exists exactly for this: a
shared buffer would be overwritten by the next datagram.

## ch9 bug hunt: the two timings

- Timing A: sampling happens after `getaddrinfo` returns and before the
  post — `cancel()` already flipped the flag, the sample is true → the
  handler receives `operation_aborted` (the resolution result dropped);
- Timing B: sampling, then `cancel()` — the flag flip cannot touch the
  already-captured `aborted` → the handler receives the resolution.
  The sampling happens **in the thread** (not the handler) because post is
  asynchronous: sampled in the handler, the gap between "cancel arrived" and
  "sampled" would depend on scheduling delay rather than semantics — the
  cancellation notice would be deferred a whole queue cycle, and timing B
  could also be misreported as cancelled.

## ch10 experiment: bypassing the strand

With `s.post` changed to `ioc.post`, handlers enter the scheduler queue
directly, both `run()` threads take and execute, and the `max_active` peak
exceeds 1 (the exact value varies with scheduling; typically 2). The strand
version's constant 1: the `running` flag compresses concurrency into
serialisation. The test for "which state needs a strand": state read and
written by multiple handlers, with requests that may arrive concurrently.

## ch10 bug hunt: reproducing the dispatch deviation

Under the `!running` version, `strand_dispatch_outside_run_test`'s **first
assertion** fails first (`ASIO_CHECK(!ran)`) — the handler executed inline
on the dispatching (main) thread. In a debugger, reading the stack from the
bottom: the test function → `strand::impl::dispatch` → `execute` → handler,
with **no** `run` / `run_one_impl` in it — the handler was never in the run
loop. Restoring `scheduler.can_dispatch() && !running`, both assertions
pass.

## ch11 experiment: the recursive chain and stack depth

The recursive `chain(1000)` never grows the stack: each layer suspends,
control returns to the engine, and when the child completes, symmetric
transfer shifts to the parent — 1,000 climbs are 1,000 shifts, not 1,000
stacked calls (a debugger at the deepest breakpoint shows only a few
frames). Changing `final_suspend` to `suspend_always`: the deepest frame
suspends with nobody to resume it (not top_level, no completion), freezing
the whole chain; the deepest timer still occupies the heap, but the other
layers wait forever — `run()` stops when the timer completes and the four
conditions empty, and the test hangs or times out. That is "a frame nobody
collects is a leak."

## ch11 bug hunt: move-only

The compile error's keywords are the **rvalue reference /
`operator co_await() &&`** — the awaitable accepts rvalues only. Correct
spelling: `co_await std::move(task);`. Move-only is safer than copying
because copied awaitables would share one coroutine handle; with both passed
to `co_spawn` — the first spawn takes the frame and sets completion, the
second overwrites the completion and resumes a running or destroyed frame.
Unique ownership is the only way destruction stays unique.

## ch12 experiment: the two hops

Hop one: `running_in_this_thread()` is 1 (the hand wrap works). Hop two: 0
— the wrap covered only hop one's completion lambda; hop two's
`timer.async_wait` completion carries no strand information and lands on any
run thread. asio's `bind_executor` writes the strand into the handler's
associated executor so the lookup chain applies layer by layer — that is
the gap between sasio's hand wrap and asio's system.

## ch12 bug hunt: strand's interface surface

asio `strand.hpp`, beyond the four delivery members, has `query` /
`require` / `prefer` (the executor property protocol's trio). Without them,
the following does not port: `asio::query(strand,
execution::context)` (asking the strand for its underlying context),
`require(strand, execution::blocking.never)` (setting execution properties),
and any generic code passing the strand as an executor-concept-satisfying
object.

## ch13 experiment: read_with_future

The ten-line implementation's essence: one promise/future hand-off, with the
handler holding the promise's `shared_ptr` alive. asio does not write a
`read_with_xxx` per form because `async_initiate` hands "how the token
becomes a completion handler" to the `async_result` specialization — the
initiation is written once, and a new token type brings its own
specialization.

## ch13 bug hunt: use_awaitable's resumer

In `asio/use_awaitable.hpp`'s specialization, the completion handler is an
object holding a reference to the awaitable frame; invoked, it writes the
result into the frame and calls `frame->resume()` — the coroutine continues
at its `co_await`. sasio's counterpart is the `awaiting_.resume()` in the
`async_operation` callback (chapter 11, step ⑤) — the same-named sibling,
simplified.

# Appendix E: The Table of Deliberate Differences

By chapter. The authoritative list is this repository README's
`Deliberate limits` section (maintained in all four language versions);
this table collects the entries the book expanded, plus where to find them
in asio.

| Chapter | Difference | Reason | where in asio |
| --- | --- | --- | --- |
| whole book | Windows + WSAPoll only | teaching scope | the 6 backends under `detail/` |
| whole book | no executors / completion tokens | the composition layer is not the engine | chapters 12–13's tables |
| ch4 | handlers as `std::function`, not operation objects | no allocator/cancellation hooks | `detail/scheduler_operation` |
| ch4 | timers and descriptors hold no work unit | the count covers queued handlers only | `scheduler.ipp`'s `outstanding_work_` |
| ch5 | the interrupter is a self-connected UDP socket | saves a pair of handles | `detail/select_reactor.hpp`'s interrupter |
| ch6 | one timer, one wait | no wait list to maintain | `detail/timer_queue.hpp`'s per_timer_data |
| ch6 | the scheduler computes timeouts | the timer queue is its own service | `epoll_reactor`'s `update_timeout` |
| ch7 | one slot per direction (no operation queue) | no object management | `reactive_socket_recv_op` |
| ch7 | sockets non-movable, accept by reference | no move semantics | `basic_stream_socket` |
| ch7 | only the `no_delay` option | enough for the book | `detail/socket_option*` family |
| **ch9** | **UDP truncation delivered as an error, not swallowed** | **no POSIX-aligning swallow of `WSAEMSGSIZE`** (a behavior difference found in review) | `detail/impl/socket_ops.ipp`'s handling of `WSAEMSGSIZE` / `ERROR_MORE_DATA` in `recvfrom` |
| ch9 | resolver: one thread per query, cancel touches the latest only | the minimum legible version | `detail/resolver_thread_pool.hpp` |
| ch9 | `udp::endpoint` reuses `tcp::endpoint` | no type gymnastics | `asio/ip/udp.hpp` |
| ch8 | composed operations take one buffer, hardcoded completion condition | no BufferSequence / policy base | `impl/write.hpp` + `base_from_completion_cond.hpp` |
| ch8 | no `async_read_until` | dynamic buffers + delimiter search, exercise-level | `asio/read_until.hpp` |
| ch10 | strand is an object, not an executor adaptor | no executor system to adapt | `asio/strand.hpp` |
| ch10 | no `bind_executor` automatic inheritance | same | `asio/bind_executor.hpp` |
| ch11 | coroutines without tokens, completion in `~promise_type` | no `async_result` system | `impl/awaitable.hpp`, `impl/co_spawn.hpp` |
| ch11 | `co_spawn` resumes directly (synchronous completion may be outside run) | presented as designed, registered | `impl/co_spawn.hpp`'s delivery start |

The bold row (ch9's UDP truncation) is the behavior difference found by the
review process, registered in the README's `Deliberate limits` list and this
table in sync.
