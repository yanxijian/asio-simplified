<!-- translated-from: book/src/ch10-strand.md sha256:c98c21cf9bfd -->

# strand: One Queue and One Running Flag

Chapter 2 said it: with multiple threads calling `run()` simultaneously,
different handlers execute concurrently. Chapters 4 and 5 supplied every part
of that concurrency — two threads each asleep in `WSAPoll`, each taking from
the queue, each executing. The cost now shows: in an echo server, the same
session's `do_read` and `do_write_some` can run **at the same time** on two
threads, and plain members like `write_offset_` instantly become data-race
targets.

asio's answer is called strand: **mark a set of handlers as "never
concurrent."** sasio's `strand` is a small object bound to the `io_context`;
`include/sasio/strand.hpp` plus `src/strand.cpp` is 272 lines — this chapter
opens it, along with a bug that just happened: a strand semantic deviation
the repository's own review process caught during the writing of this book,
sitting in the commit history beside chapter 5's two missed wake-ups.

## Design: Queue, Flag, Drain Loop

The strand's internal state is three things:

```cpp
scheduler& scheduler;                     // the drain borrows the scheduler
std::mutex mutex;
std::deque<std::function<void()>> queue;  // queued handlers
bool running = false;                     // is a drain loop running
```

Two invariants:

1. **Handlers of the same strand never run concurrently, and `post` order is
   execution order**. The implementation does not lock the handlers
   themselves; it ensures **at most one drain loop runs at any time** — the
   `running` flag is that "logical lock";
2. **A throwing handler cannot deadlock the strand**. The drain loop releases
   `running` on the exception path, reschedules the remaining handlers, and
   lets the exception keep flying — the serialisation guarantee neither
   swallows exceptions nor breaks under them.

Note that `running` has no blocking semantics like `std::mutex::lock`: a
handler that cannot get the strand does not wait, it **queues** — that is how
"no concurrency" and "never block the caller" coexist. The queueing itself is
protected by the strand's own mutex (fine-grained, tiny critical sections);
the serialisation is guaranteed by `running`.

## The Life of One Drain

From `s.post(h)`, h's life has four steps:

1. **Enqueue**: h enters `queue`. Under the lock, check `running`: if no
   drain is running, set it true and note "a drain must be scheduled";
2. **Schedule**: `post` an anonymous function into the scheduler — it does
   one thing, call `drain()`. To the scheduler this anonymous handler is
   ordinary: queued, executed, holding a work unit; the strand has no
   special status;
3. **Drain**: `drain()` loops — take one from `queue` under the lock, execute
   it outside the lock, and when the queue is empty set `running` false and
   return. Handlers posted during execution merely enter the queue and are
   taken by **the same** loop (next pass);
4. **Finish**: queue empty, `running` false. The next post sees an idle
   strand and starts from step 1.

The elegance is in step 3: **the drain loop binds "execution right" to "the
queue."** While `running` is true, every `s.post` in the world merely
appends — execution stays in the drain's hands, and the order is naturally
the post order (the second half of invariant 1).

The drain's exception handling deserves its own look:

```cpp
try
{
  execute(std::move(handler));
}
catch (...)
{
  reschedule_after_exception();
  throw;                          // the exception keeps flying out of run()
}

void strand::impl::reschedule_after_exception()
{
  bool more = false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
    if (!queue.empty())
    {
      running = true;
      more = true;
    }
  }
  if (more)
    schedule_drain();
}
```

When the exception flies out, `running` is still set — unhandled, the strand
would be locked forever. `reschedule_after_exception` releases it, reschedules
the remaining handlers (a fresh `post`), and `throw`s so the exception follows
asio's rule out of `run()`. The exception section of `strand_test` verifies
exactly this: two throwing handlers sandwiching three normal ones, three
rounds of `run()`, ending with count == 3 and exception_count == 2.

## dispatch's Two Paths: One Shared Criterion

The strand has `post` and `dispatch` entries. `post` is simple (always
queue); `dispatch` means "run in place if you can" — **but what "can" means
must agree between the strand and the scheduler**. The code first, from
`src/strand.cpp`'s `impl::dispatch`:

```cpp
if (current_strand_impl == this)        // already inside this strand: inline
{
  execute(std::move(handler));
  return;
}

bool run_now = false;
bool schedule = false;
{
  std::lock_guard<std::mutex> lock(mutex);
  // asio: can_dispatch() && !locked_
  if (scheduler.can_dispatch() && !running)
  {
    running = true;
    run_now = true;                     // inline execution
  }
  else
  {
    queue.push_back(std::move(handler));
    if (!running)
    {
      running = true;
      schedule = true;                  // idle strand + outside run: queue + drain
    }
  }
}
// run_now → execute + drain; schedule → schedule_drain()
```

Three branches, one test: **the calling thread is inside `run()`, and the
target is currently idle**. The scheduler's `dispatch` looks at the context
(`current_scheduler == this`), the strand looks at the strand itself
(`current_strand_impl == this`) — different objects, same principle. asio's
two implementations read `can_dispatch()` (scheduler.ipp) and
`can_dispatch() && !locked_` (strand_service.ipp); sasio is isomorphic to
both.

This "shared test" was not born — it was fixed. Early sasio's
`strand::dispatch` looked only at `running`: an idle strand meant inline
execution — **even if the calling thread was not inside `run()`**. So
`strand s(ctx); s.dispatch(h);` ran h before `run()` ever started, violating
chapter 2's contract two outright. The repository's review process caught it;
the fix aligns with asio `strand_service::do_dispatch`'s
`can_dispatch() && !locked_`, plus a regression test:

```cpp
void strand_dispatch_outside_run_test()
{
  io_context ioc;
  strand s(ioc);

  bool ran = false;
  s.dispatch([&ran] { ran = true; });

  ASIO_CHECK(!ran);      // fails before the fix: the old code ran it inline
  ioc.run();
  ASIO_CHECK(ran);       // the handler executed inside run()
}
```

Lay the three inline-execution paths side by side — the full ledger for
chapter 2's contract one:

| Path | Inline condition | Object tested |
| --- | --- | --- |
| `scheduler::dispatch` | `current_scheduler == this` | context |
| `strand::dispatch` | `can_dispatch() && strand idle` | strand |
| `strand::dispatch` (already inside the strand) | `current_strand_impl == this` | strand |
| Timer expiry (`run_one_impl`) | invoked directly by the run loop, not queued | — |

The nesting semantics sit in the table too: a thread already holding the
strand always dispatches inline (asio same — the test `increment_with_lock`
asserts exactly that).

## Close Reading: thread_local, Pushed and Popped Twice

`execute` and `running_in_this_thread` share one thread_local variable:

```cpp
void execute(std::function<void()> handler)
{
  void* previous = current_strand_impl;
  current_strand_impl = this;          // push

  struct restore
  {
    void* previous;
    ~restore() { current_strand_impl = previous; }   // pop
  } guard = {previous};

  handler();
}
```

It stores not "which strand this thread is on" but a **stack** — `previous`
saves the outer level and the destructor restores it. Why a stack? Because a
handler may `dispatch` a handler onto **another** strand: the inner strand's
`execute` sets the variable to itself, and on return restores the outer
value. Nested strands thus answer `running_in_this_thread` correctly at
every level.

Against asio: it stores no pointer, instead attaching the strand's impl into
the handler's associated executor, with `running_in_this_thread` comparing
the executor's call stack. The thread_local stack is sasio's minimal
equivalent without executors, semantically identical: **"which strand am I
on" is a dynamic fact of execution, not a compile-time ownership**.

## Comparison Table

| Question | asio | sasio |
| --- | --- | --- |
| Shape | `strand<Executor>` executor adaptor + `detail/strand_service` | an object bound to the io_context + `impl` |
| Drain | `strand_impl::run()` (driven by operation completion) | the `drain()` loop |
| Inline test | `can_dispatch() && !locked_` | `can_dispatch() && !running` |
| Nesting detection | the executor's call stack | `thread_local` + save/restore |
| Binding syntax | `bind_executor(strand, handler)` | `s.post(...)` / hand wrapping (below) |
| Source entry | `strand.hpp`, `detail/impl/strand_service.ipp` | `src/strand.cpp` |

"Cannot inherit automatically" is the most valuable entry in sasio's
differences table: asio's
`async_read(socket, buf, bind_executor(strand, h))` makes **every
intermediate completion** of the composed operation go through the strand;
sasio's hand wrapping covers only the layer you explicitly wrapped.
`examples/ex04_bind_executor.cpp` shows the wrapping in ten lines, and what
it cannot do — the principle of that gap (the associated executor lookup
chain) is chapter 12's subject.

## Exercises

**Experiment**: run the multi-threaded case in `tests/asio/strand.cpp`, then
a destruction experiment — change every `s.post(...)` in
`strand_multithread_test` to `ioc.post(...)` (bypassing the strand) and run
again. Watch the `max_active` assertion: without the strand, the peak of
concurrently executing handlers is no longer 1. That contrast is the
strand's entire value, and the test for "which state needs strand
protection."

**Bug hunt**: the strand dispatch bug from this chapter's opening sits in the
repository history. Open `tests/asio/strand.cpp`, rename
`strand_dispatch_outside_run_test` so it will not run, and temporarily change
`src/strand.cpp`'s inline condition from
`scheduler.can_dispatch() && !running` back to `!running` (restoring the
original deviation). Rebuild and run. Which assertion fails first? On which
thread did the handler execute (check the call stack in a debugger — the
bottom is `strand_dispatch_outside_run_test`, not `run`)? Restore the correct
implementation. You have just reproduced this repository's first
review-caught bug.

---

With serialisation in place, the callback-style asio is complete. The last
modern piece remains: writing callbacks as straight lines — `co_await`. Next
chapter opens `awaitable<T>` and `co_spawn`: promises, symmetric transfer,
and the completion handler hiding inside a destructor.
