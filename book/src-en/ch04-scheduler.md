<!-- translated-from: book/src/ch04-scheduler.md sha256:1feee4a767fd -->

# scheduler: The Handler Queue and the Work Count

Chapter 3 left the scheduler sitting in the container as a black box. This
chapter opens it. Start with code you have already used — the exception case
from chapter 1's exercise, taken from `tests/asio/io_context.cpp`:

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

Three rounds of `run()`, two exceptions, all three counting handlers executed.
Four questions hide behind this test: after a handler throws, do the
remaining ones still run? How does `run()` know "there is still work, another
round is worthwhile"? Who collects the resources of the half-executed handler
that threw? And the most fundamental one — what is `run()` looping over?

`scheduler`'s 83-line `run_one_impl` plus about thirty lines of bookkeeping
answer all four.

## Design: One Counter Over Three Kinds of Work

The data structures first. The scheduler holds a queue, a counter, and a stop
flag:

```cpp
std::mutex mutex_;
std::deque<std::function<void()>> queue_;   // handlers waiting to run
std::size_t work_count_ = 0;                // outstanding work units
std::atomic<bool> stopped_{false};
```

Three invariants — memorize them before reading on:

1. **Every pending queue handler corresponds to one work unit, released as
   soon as it finishes — even if the handler throws.** "Pending" here means
   queued handlers only; timers and descriptors do not hold a count, but
   they also prevent stopping (invariant 2);
2. **When `work_count_` is zero, the queue is empty, the timer heap is empty,
   and no descriptor is registered, the scheduler stops itself**. With an
   `io_context::work` held, it does not stop even when the other three are
   empty. After stop, the queue is kept intact; only `restart()` resumes
   execution;
3. **Handlers execute one per pass, taken from the front of the queue**.
   Before execution a thread marker is set (dispatch's inline test depends
   on it); exceptions fly up the call stack and out of `run()`, uncaught
   here.

Invariant 1's "released as soon as it finishes" is honored by a small RAII
class. `handler_work` does nothing at construction (the unit was counted at
`post` time) and releases at destruction:

```cpp
class handler_work
{
public:
  explicit handler_work(scheduler* s) : scheduler_(s) {}
  ~handler_work() { scheduler_->on_work_finished(); }
  // ...
};
```

This object lives exactly as long as the work unit is outstanding — it plays
the role of asio's `work_cleanup`. Combining it with `invoke`: when an
exception flies out of the handler, `handler_work`'s destructor still runs
during stack unwinding and the count drops correctly; the exception keeps
flying and escapes `run()` to the caller. That is asio's behavior: **the
exception is yours, the bookkeeping is the framework's**.

## Close Reading: run_one_impl in Full

Now dissect the 83-line loop. It is the only place in the library that ever
blocks, and the meeting point of all three services (queue, timer heap,
reactor). Piece by piece.

**Entry: run()'s precondition check.**

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

An idle call stops immediately and returns 0 — asio's behavior too: `run()`
with nothing to do puts the context into the stopped state, and even if you
post something afterwards, it takes `restart()` to run. `poll()`, `run_one()`,
and `poll_one()` carry the same check.

**Loop step 0: the stop flag first.**

```cpp
for (;;)
{
  if (stopped())          // once stopped, no further handler is invoked
    return false;
```

Stopping means "quit now, queue untouched": the handler already dequeued
finishes (the current one), the rest do not move, waiting for `restart()`.
The test's "after the exception escapes, re-run `run()` and the remaining
handlers execute" verifies exactly this.

**Step 1: the queue has a handler — take one, run one.**

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

Note the lock's scope: it protects only the dequeue. The handler **executes
outside the lock** — if execution held the lock, any `post` inside the
handler (which takes the same lock) would deadlock instantly. `run_one_impl`
returning true means "one unit of work done this pass"; `run_impl` is a
counting shell: `while (run_one_impl(block)) ++count;`.

`handler_work` is constructed here — from the dequeue until execution ends,
this handler's unit stays on the books, released by the destructor. Invariant
1, honored.

**Step 2: the queue is empty — check the timers.**

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

An expired timer's handler is invoked **directly inside the run loop** —
recall contract one from chapter 2: I/O completions go through post, timer
completions take this path. `handler_scope` sets the thread marker (so
dispatch's inline test works), then `complete(error_code())` — an empty
error_code, "expired normally". Afterwards, `maybe_stop_if_idle()`: if this
was the last pending item, running it should stop the context.

**Step 3: both sources empty — decide how long to sleep.**

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

`has_work_locked()` is invariant 2 in code, all four conditions at once:

```cpp
return work_count_ != 0 || !queue_.empty() || !timers_.empty()
    || !reactor_.idle();
```

The sleep duration is one of three: with no timers, sleep indefinitely
(wakeable only by a `post`, a new descriptor registration, or `stop` — how
"being woken" works is chapter 5's subject); with timers, sleep until the
earliest expiry; under `poll()` semantics, one non-blocking pass (the
`polled` flag keeps `poll()` from spinning on the reactor).

After `reactor_.run_for(timeout)` wakes, control returns to the top of the
loop — re-check the stop flag, re-check the queue. Chapter 2's "re-check
every source, every pass" is exactly this, in full.

## The Life of a Work Unit

The work unit behind invariants 1 and 2 has exactly three lives:

- **Queue**: `post` does `++work_count_`; when the handler finishes, the
  `handler_work` destructor does `--work_count_`;
- **Guard**: `io_context::work w(ctx);` does `++work_count_` at construction
  and `--work_count_` at destruction (or `reset()`). It means "I still have
  work to submit — do not stop"; the classic use is a main thread holding a
  work guard in a multi-threaded server so worker threads never drain the
  context;
- **Async resolution** (chapter 9): the resolver's worker thread holds one,
  keeping `run()` alive while `getaddrinfo` has not returned.

The count reaching zero does not stop the context by itself — it makes "all
four empty" true, and the next check acts on it: `on_work_finished()` calls
`maybe_stop_if_idle()` immediately after decrementing, so `stopped_` is set
on the spot if the four conditions hold, and the run loop observes it at the
top of its next pass. asio's `work_finished()` behaves the same way.

## Comparison Table

| Question | asio | sasio |
| --- | --- | --- |
| The loop | `detail/impl/scheduler.ipp` `do_run_one()` | `src/scheduler.cpp` `run_one_impl()` |
| Work count | `outstanding_work_` | `work_count_` |
| Execution-time bookkeeping | operation's `work_cleanup` | `handler_work` RAII |
| Immediate completion | `post_immediate_completion()` | plain `post` |
| Stop condition | `outstanding_work_ == 0` (optionally uncounted at construction) | four conditions (timers and descriptors included) |
| Guard | `executor_work_guard` | `io_context::work` |

One deliberate difference deserves a callout: asio's `run()` distinguishes
"queue only, no timers" differently from sasio (asio's queued handlers also
count as work, so its four conditions collapse into one; sasio's timers and
descriptors are uncounted and checked separately). The **observable
behavior** matches: with any kind of pending work, `run()` does not exit.

## Exercises

**Experiment**: manufacture a work leak and watch `run()` hang:

```cpp
sasio::io_context ctx;
sasio::io_context::work guard(ctx);   // forgot to reset
ctx.post([] { std::puts("done"); });
ctx.run();                            // never returns — why?
std::puts("never printed");
```

Run it (then Ctrl+C). Describe the stuck loop in this chapter's terms: which
three of the four conditions are empty, which one is not, and on which line
of `run_one_impl` every pass goes to sleep. Comparison case: a second context
with nothing posted, whose guard is `reset()` immediately — `run()` runs no
handler and returns **0**:

```cpp
sasio::io_context ctx2;
sasio::io_context::work guard2(ctx2);
guard2.reset();                       // the unit goes back early
std::size_t n = ctx2.run();           // all four empty → stop, returns 0
std::puts(n == 0 ? "clean exit" : "unexpected");
```

Side by side, both "what keeps run() alive" and "what run() returns" become
clear.

**Bug hunt**: return to chapter 1's three questions — you can answer them
yourself now:

1. When the first handler throws, **it has already been dequeued**; its
   exception has nothing to do with the queue. The normal handlers remain
   intact and continue after the restart — so count == 3;
2. `stopped()` is **false**: an exception is not a stop instruction. The stop
   flag is only set by "all four empty" or an explicit `stop()` — it was
   never set here, which is why re-calling `run()` inside catch is legal;
3. Three rounds because each `run()` processes at least one handler before
   the exception interrupts the counting shell; the remaining handlers
   execute over the following rounds until all three have run, the queue is
   empty, all four conditions hold, `run()` returns 0 cleanly, and the loop
   breaks.

Check your predictions. If all three hit, you are already thinking about this
loop the way its implementer does.

---

The loop, the count, and stopping are all in place. But one line of step 3
remains a black box: `reactor_.run_for(timeout)`. How is `WSAPoll` called,
how is the descriptor table managed, and how does another thread wake the
sleeper? Next chapter opens the reactor — where you will meet "the two
missed wake-ups," two real bugs buried inside that one line.
