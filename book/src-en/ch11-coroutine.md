<!-- translated-from: book/src/ch11-coroutine.md sha256:67c19c8bed8d -->

# Coroutines: Callback Chains Written as Straight Lines

Chapter 8's `write_op` glued one layer of callbacks into a chain with a
shared_ptr relay; a real protocol needs more — "read the header, then the
body, parse, write back, loop," four or five async steps in sequence, and in
callback style it grows like this:

```cpp
void do_read_header(std::shared_ptr<session> self)
{
  async_read(self->socket_, header_buf,
      [self](error_code ec, std::size_t) {
        if (ec) { close(self); return; }
        self->parse_header();
        async_read(self->socket_, body_buf,
            [self](error_code ec, std::size_t) {
              if (ec) { close(self); return; }
              // ...each step deeper, each piece of state further away
            });
      });
}
```

The control flow is shredded into nested lambdas: shared state lives in
`self` captures, error handling is copied per layer, and early exits must be
traced by hand. C++20 coroutines write the same logic as straight lines:

```cpp
sasio::awaitable<void> session(tcp::socket& socket)
{
  char header[16];
  co_await sasio::async_read_some(socket, sasio::buffer(header));
  // parse_header() sits right here
  char body[64];
  co_await sasio::async_read_some(socket, sasio::buffer(body));
  // ...
}
```

This chapter opens `include/sasio/awaitable.hpp` and `co_spawn.hpp` to answer
three questions: what happens when `co_await` suspends? Where does the
completion notification come from when the coroutine ends? And why does
sasio's coroutine not need asio's completion-token machinery?

## Design: A Task Type and Its Promise

`awaitable<T>` is a **coroutine return type** — any function declaring it can
`co_await` and `co_return` in its body. The compiler rewrites the body into a
large state machine, and the state machine's bookkeeping rules come from
`promise_type`. sasio's promise has five members, each answering one question
of the coroutine's life:

```cpp
struct promise_type
{
  std::optional<T> value;        // co_return lands here
  std::exception_ptr error;      // unhandled_exception lands here
  std::function<void(std::exception_ptr, T)> completion;  // top level: who gets the result
  std::coroutine_handle<> continuation;                   // nested: who gets control back
  bool top_level = false;        // set by co_spawn; decides the ending
};
```

Three invariants:

1. **The awaitable exclusively owns the coroutine frame**. It is move-only
   (copies deleted), and `co_await` accepts only rvalues — `co_await task`
   on a named variable does not compile; write
   `co_await std::move(task)`. Not showing off: the right to destroy the
   frame must have exactly one owner, and two code paths both believing they
   hold it is a double free;
2. **Top-level and nested endings differ**. A top-level coroutine started by
   `co_spawn` does **not** suspend at `final_suspend` — the frame destroys on
   the spot, and the completion handler runs in the promise's destructor; a
   nested coroutine awaited by someone suspends at `final_suspend` and hands
   control back to its waiter through symmetric transfer, its frame released
   by the waiter;
3. **Async completions return to the run loop; synchronous completions return
   to the initiating stack**. Whoever resumes the coroutine, on whose stack
   the notification lands: a suspended coroutine is resumed by the engine's
   completion delivery (inside `run()`); a coroutine that `co_return`s
   without ever suspending has its frame destroyed inside `co_spawn`'s first
   `resume()` — the completion handler executes on **co_spawn's call stack**,
   possibly outside `run()`. Know which stack your handler is on.

## The Life of One Coroutine

Trace the real test case:

```cpp
sasio::awaitable<int> return_after_wait(sasio::steady_timer& timer)
{
  co_await sasio::async_wait(timer);
  co_return 42;
}

// in the test:
co_spawn(ioc, return_after_wait(timer),
    [&](std::exception_ptr e, int result) { value = result; });
ioc.run();
```

From `co_spawn` to the landing `42`, eight steps:

```text
① co_spawn: take the handle, set top_level=true, store completion, resume()
② the body runs → co_await async_wait(timer)
③ async_wait builds async_operation<void>, co_awaits it
    → await_suspend: remember awaiting_ (this coroutine's handle), call start(...)
    → start calls timer.async_wait(handler), registering in the heap
    → async_operation::await_suspend returns → the coroutine suspends,
      control returns to co_spawn
④ co_spawn returns (the coroutine is asleep on the timer)
⑤ the timer expires → the run loop invokes the timer's completion handler
    → the handler calls awaiting_.resume(): the coroutine continues at co_await
⑥ async_operation::await_resume: error_ empty → normal return
⑦ co_return 42 → promise::return_value(42) → final_suspend()
    → final_awaiter::await_ready() == true (top_level) → no suspend
    → the frame destroys on the spot → ~promise_type calls completion(no error, 42)
⑧ the test's lambda receives 42
```

Step ③ deserves a stop: **the coroutine's suspension point is "register one
async operation."** `async_operation` wraps a callback-style initiation
(`timer.async_wait(...)`) into a co_awaitable object — initiate, register,
suspend in one breath. The bridge between callback style and coroutine style
is this one structure, and both banks are the same engine.

## Close Reading: final_awaiter and the Completion Hidden in a Destructor

The coroutine's ending is the twistiest part of the implementation; sasio
compresses it into 20 lines:

```cpp
struct final_awaiter
{
  promise_type* self;

  // Top-level coroutines (started by co_spawn) do not suspend at the end:
  // the frame is destroyed automatically and ~promise_type runs the
  // completion handler. Awaited coroutines do suspend and hand control to
  // their awaiting coroutine via symmetric transfer.
  bool await_ready() const noexcept { return self->top_level; }

  std::coroutine_handle<> await_suspend(
      std::coroutine_handle<promise_type> handle) noexcept
  {
    std::coroutine_handle<> next = handle.promise().continuation;
    return next ? next : std::noop_coroutine();
  }

  void await_resume() noexcept {}
};
```

After `co_return`, the compiler calls `final_suspend()` and receives this
awaiter:

- **Top level** (`top_level == true`): `await_ready` returns true, skipping
  the suspension — the body finished and the frame **destroys on the spot**.
  `~promise_type` is the last stop of frame destruction, and the completion
  notification leaves from there:

  ```cpp
  ~promise_type()
  {
    if (completion)
    {
      try
      {
        completion(error, value ? std::move(*value) : T{});
      }
      catch (...) {}
    }
  }
  ```

  The exception and the return value merge into one call; whatever the
  completion throws is swallowed — "handing over the result" is the
  coroutine's last act, and a throw after it has no receiver. Note the
  completion handler is invoked **inline**: on the thread of the last
  resume — which came from the engine's completion delivery (step ⑤), so
  contract two holds.

- **Nested**: `await_ready` returns false and `await_suspend` runs — its
  return value is **the next coroutine handle to resume** (the waiter), or
  `std::noop_coroutine()` if there is none. The compiler jumps to that
  handle directly, **adding no call-stack level** — that is symmetric
  transfer. Without it, a deep chain of coroutines stacks one frame per
  return, and a long chain overflows the stack; with it, control passes like
  a relay baton.

Why destroy the top level "on the spot" instead of suspending for someone to
collect? Because the top level has no waiter — nobody but the completion
knows it exists. Suspending a coroutine nobody will resume leaks a frame.
"No suspend → destroy → the destructor hands over the result" welds that
leak shut.

## Comparison Table

| Question | asio | sasio |
| --- | --- | --- |
| Task type | `awaitable<T, Executor>` (with executor) | `awaitable<T>` (no executor) |
| Frame bookkeeping | `impl/awaitable.hpp`'s `awaitable_frame_base` (inherits `awaitable_launch_context`, custom `operator new` recycles frames) | `promise_type` + handle ownership |
| Start | `co_spawn` (delivered through the executor) | `co_spawn` (direct `resume()`) |
| Completion notice | `async_result` specialization + token (`use_awaitable`) | `promise_type::completion` |
| Suspend/resume | `awaitable_frame::resume()` | `awaiting_.resume()` |
| Source entry | `awaitable.hpp`, `impl/awaitable.hpp`, `impl/co_spawn.hpp` | `include/sasio/awaitable.hpp`, `co_spawn.hpp` |

The scale gap's root is row 4. asio's `co_spawn(ctx, task, token)` accepts
`use_awaitable`, `use_future`, detached... "how the result comes back" is
decided by the token, so the completion must pass through the `async_result`
specialization system (chapter 13's subject). sasio cuts the token and writes
the completion directly in the promise's destructor — a one-liner. The price
is on the record too: no `use_future`, no detach, one free-function adapter
per operation (`co_spawn.hpp`'s `async_wait` / `async_read_some` and
friends).

Another scale difference: asio's `awaitable_frame_base` inherits
`awaitable_launch_context` and customizes `operator new` for **frame memory
recycling**; co_spawn's work count hangs on `co_spawn_state`'s work guard
(`impl/co_spawn.hpp`), not in the frame. sasio's coroutine frame is "bare":
memory goes to the compiler's coroutine machinery, and work depends on the
**underlying operations** (the timer and socket registrations). Same effect
(while the coroutine waits, `run()` does not exit), different mechanism —
asio manages frames as a resource pool; sasio treats them as plain objects.

## Exercises

**Experiment**: write a **recursive** coroutine chain to verify symmetric
transfer does not eat the stack. Note that "awaiting sequentially a thousand
times" is not nesting — at any moment only one layer is waiting, and stack
depth has nothing to do with it. To build a real deep chain, have a
coroutine await a coroutine await a coroutine:

```cpp
sasio::awaitable<int> chain(sasio::steady_timer& t, int depth)
{
  if (depth == 0)
  {
    co_await sasio::async_wait(t);            // the only suspension, at the bottom
    co_return 0;
  }
  co_return co_await chain(t, depth - 1);     // recursion: 1000 frames alive at once
}

// co_spawn(ioc, chain(timer, 1000), [&](std::exception_ptr e, int r) { ... });
```

`chain(1000)` keeps 1000 coroutine frames alive at once (each waiting for the
next); note that of the 1000 suspension points **only the deepest one is a
timer** — the other 999 wait for "their child coroutine." When the timer
fires, the completion climbs the 1000 `co_return`s one layer at a time —
every hop is a symmetric transfer, and in a debugger the call stack stays
flat (compare a plain recursive function: 1000 calls, 1000 stack frames).
Once it works, change `chain`'s `final_suspend` to `std::suspend_always{}`
(suspend, and nobody resumes it) and run again — the chain freezes at some
layer, and `run()` stops early because the timer chain broke. That is
invariant 2's negative textbook: **a frame nobody collects is a leak**;
asio's awaitable_frame backs it with a frame-recycling pool, sasio with the
convention "top level always destroys, nested always awaited."

**Bug hunt**: this code fails to compile at the `co_await` line:

```cpp
auto task = return_after_wait(timer);
co_await task;        // ← compile error
```

Two questions: which words in the error point at the cause? What is the
correct spelling? And deeper — why is move-only safer than allowing copies?
Hint: if two copies of one awaitable were both co_spawned, who destroys the
coroutine frame?

---

Callback style and coroutine style are both complete. But before chapter 12
the book kept mentioning a "cut composition layer": executors and completion
tokens. What makes them worth tens of thousands of lines of the 169,000? The
next chapter opens the executor.
