<!-- translated-from: book/src/ch06-timer.md sha256:2d8e5dfc5395 -->

# Time: timer_queue and steady_timer

The run loop asks the timer queue two questions: "when does the next one
expire" (how long to sleep) and "is anything due now" (whether to work).
Chapter 5's wake-up path added a third: "is the new timer the earliest" (wake
or not). All three are answered by `timer_queue`, a textbook min-heap — but
this file of fewer than 140 lines buries the only true bug in the book that
asio's own test suite caught. This chapter dissects it.

## Design: Heap, Index, Lifetime

A pending timer is one `entry`:

```cpp
struct entry
{
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  clock::time_point when;                    // expiry moment
  std::function<void(error_code)> handler;   // who to call
  std::size_t heap_index = npos;             // my position in the heap
};
```

The heap itself is `std::vector<std::shared_ptr<entry>>`, a min-heap ordered
by `when`. Two invariants:

1. **Heap order holds, and every entry in the heap has a `heap_index`
   matching its actual position**; an entry not in the heap must carry
   `npos`. Both halves matter — the first keeps `front()` the earliest
   expiry, the second keeps "cancel" from groping the heap with a stale
   index;
2. **A cancelled completion is never invoked in place**. `cancel()` from any
   call stack (even inside another handler's stack) delivers the cancelled
   handler only via `post` — reentrancy is the subtlest landmine in async
   code, and this invariant removes it.

The entry is held by `shared_ptr` because it **lives in two worlds at once**:
one reference in the heap, one in the timer object's `pending_`. Whoever
moves first decides — `cancel()` grabbing the lock first moves the handler
out and posts the abort; `take_expired` grabbing it first moves the handler
out and delivers success. The mutex narrows the race to an atomic instant;
neither side ever sees a half-finished entry.

## Close Reading: pop_at and the Real Bug

The heap's core operation is "remove the element at an arbitrary position."
Removing the top (`take_expired` calls `pop_at(0)`) and cancelling
(`remove` calls `pop_at(anywhere)`) share the code:

```cpp
void timer_queue::pop_at(std::size_t index)
{
  if (index >= heap_.size())
    return;

  const std::shared_ptr<entry> removed = heap_[index];
  const std::size_t last = heap_.size() - 1;

  // Move the last element into the hole. Do not use swap_at() here: it would
  // write the removed entry's heap_index back to a valid-looking value, which
  // is exactly the stale index that made a later remove() index out of bounds.
  if (index != last)
  {
    heap_[index] = std::move(heap_[last]);
    heap_[index]->heap_index = index;
  }

  heap_.pop_back();
  removed->heap_index = entry::npos;      // ← the fix: leaving the heap is npos

  if (index < heap_.size())
  {
    sift_down(index);
    sift_up(index);
  }
}
```

That comment — "Do not use swap_at() here" — is the chapter's crux. The
textbook way to delete from a heap is: move the last element into the hole,
`pop_back`, then sift at the new position. The lazy shortcut reuses the
existing `swap_at`:

```cpp
// the buggy version:
swap_at(index, last);    // swap_at rewrites BOTH entries' heap_index!
heap_.pop_back();
```

`swap_at`'s job is heap order, and it faithfully updates **both** elements'
indices — including the entry about to be deleted. After the delete,
`removed->heap_index` points at a position that is no longer legal. Concrete
numbers: two timers in the heap, cancel the top. `pop_at(0)`: `index=0`,
`last=1`, after the swap the removed entry's `heap_index` is 1; `pop_back`
shrinks the heap to size 1. Now `removed->heap_index == 1 == heap_.size()` —
an out-of-bounds value, settled.

When does it explode? The entry is held by `steady_timer`'s `pending_`; the
timer object's destructor calls `cancel()`, which calls `remove()`, which
looks up `heap_index == 1` in a heap of size 1 — `heap_[1]` is out of bounds,
and the Debug STL assertion fires on the spot
(`vector subscript out of range`).

Why did the bug survive? It needs **two** timers: with one timer,
`index == last`, no swap, no out-of-bounds, and emptying the heap is
uneventful. The original hand-written test happened to use a single timer.
Porting asio's own `deadline_timer` tests — which place two timers side by
side — detonated the Debug assertion immediately. That is the story behind
the book's "test porting caught a real bug." The fix has two layers: `pop_at`
no longer uses `swap_at`, and leaving the heap sets `npos` (the root cause);
plus `remove` opens with a **bounds + identity** double check on the index —

```cpp
const std::size_t index = e->heap_index;
if (index == entry::npos || index >= heap_.size() || heap_[index] != e)
{
  e->heap_index = entry::npos;
  return false;
}
```

Even if a dirty index ever appears again, `remove` quietly returns "not in
the heap" instead of reaching out of bounds. Root-cause fix plus defensive
check, each minding its own layer.

The same `add` moonlights as chapter 5's wake-up signal — it returns whether
the new entry became the earliest:

```cpp
const bool earliest = heap_.empty() || e->when < heap_.front()->when;
// ...push, sift up...
return earliest;      // steady_timer decides on wake_waiters() from this
```

One return value lets the "second missed wake-up" fix leave the heap cleanly —
the heap does not know the reactor exists, so the lock order
(scheduler → timer → reactor) stays intact.

## steady_timer: Waiting, Cancelling, Destroying

The heap is the machinery; `steady_timer` is the entry point the user operates. Three
operations, one look each.

**`async_wait`**: packs `{expiry_, handler}` into an entry and hangs it in the
heap. One timer object allows one pending wait — a second `async_wait`
cancels the first (the old wait completes with `operation_aborted`, the new
one enters the heap). asio allows any number of waits per object; that
simplification sits in sasio's differences table.

**`cancel`**: takes the `pending_` reference (`std::move`), asks the heap to
remove it. Removal succeeds means the wait had not expired — move the handler
out of the entry and `post` a completion carrying `operation_aborted`:

```cpp
// Never invoke the handler from inside cancel(): post it, exactly like asio.
scheduler_.post([handler = std::move(handler)]() mutable {
  handler(operation_aborted);
});
```

Removal fails means the wait **had already expired, its completion is already
in flight** (the scheduler's `take_expired` took the handler) — `cancel`
returns 0 and does nothing. Note the lock discipline: `take_expired` moving
the handler out of the entry and `cancel`'s `remove` are serialized by the
same `timer_queue::mutex_`, so there is no intermediate state where both or
neither get the handler. Invariant 2 holds here: from whichever stack
`cancel` is called, the handler executes via `post` in the run loop, never
re-entering.

**Destruction**: `~steady_timer` is `cancel()` — the object dies, and its
pending wait completes with `operation_aborted`. asio's semantics match; the
cost is that destroying a "still waiting" timer injects a completion handler
into the queue, so **destruction timing** needs awareness (this is why the
coroutine adapters in co_spawn.hpp hold timers by reference — the coroutine
lives, the timer must live).

## Comparison Table

| Question | asio | sasio |
| --- | --- | --- |
| Heap | `detail/timer_queue.hpp` (one per clock type) | `src/timer_queue.cpp` (steady_clock only) |
| Timer identity | `per_timer_data` (a wait list per timer) | `shared_ptr<entry>` (at most one wait per object) |
| Multiple waits | yes; `expires_at` cancels all | no; a new wait auto-cancels the old |
| Cancel granularity | `cancel` / `cancel_one` | `cancel` |
| Completion path | completion op into the scheduler queue | expiry invoked directly (inside the scheduler loop); cancel via `post` |
| Relation to the reactor | timer queue registered with the reactor, reactor computes poll timeout | timer queue is an independent service; scheduler computes the timeout |

Two differences deserve elaboration. First, asio hangs the timer queue in the
reactor and lets the reactor compute the poll timeout; sasio hangs it in the
scheduler and computes there — the location differs, but the lesson "the
timeout is a snapshot; a new earliest timer must wake" was learned on both
sides (asio's `schedule_timer` interrupt, sasio's `wake_waiters`). Second,
dropping multiple waits is not laziness: their value is "one timer object as
a shared handle for several waits," and sasio's coroutine adapters (chapter
11) never use that — each co_await gets its own timer, and a single wait
suffices.

## Exercises

**Experiment**: reproduce the `pop_at` out-of-bounds bug by hand. Open
`src/timer_queue.cpp`, restore the old implementation the comment describes
(use `swap_at(index, last)`, delete the `removed->heap_index = entry::npos;`
line), and write a ten-line two-timer case:

```cpp
sasio::io_context ctx;
sasio::steady_timer t1(ctx), t2(ctx);
t1.expires_after(std::chrono::milliseconds(50));
t1.async_wait([](sasio::error_code) {});
t2.expires_after(std::chrono::milliseconds(300));
t2.async_wait([](sasio::error_code) {});
// t1 expires → pop_at(0) takes the swap path → the other entry's index goes dirty
// t1/t2 destruct → cancel → remove reads the dirty index → Debug assert fires
```

Build a Debug binary and run it; watch when and where the assertion pops —
note it does not fire in `pop_at` but **later**, in some `remove`'s
`heap_[index]`. Restore the correct code and the assertion vanishes. The
exercise gives you a feel for a class of bug: in a structure whose "indices
must always match positions," the dirty index detonates far from the crime
scene.

**Bug hunt**: `steady_timer::cancel` returns 0 directly when
`timers_.remove(entry)` fails — posting nothing. But the entry's handler may
still be sitting in the entry, and the entry may be about to be dropped with
`pending_`. Does the handler get lost, never called? (Hint: `remove` fails
for exactly three reasons — index npos, index out of bounds, identity
mismatch. Walk each reason: in that case, who is responsible for the
handler's completion? The answer hides in when `take_expired` holds the
lock.)

---

Queue, readiness, time — the engine's three core elements are all dissected, and
all wake-up paths are in place. The next chapters put this engine to work:
`socket`'s `async_read_some` "stores state, registers interest" at
initiation, and only touches the system call in `do_read` after readiness —
the two-phase shape shared by all of asio's socket code.
