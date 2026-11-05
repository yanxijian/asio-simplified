<!-- translated-from: book/src/ch05-reactor.md sha256:c48f47d82926 -->

# reactor: Readiness, Wake-ups, and the Descriptor Table

Chapter 4 left one line unopened:

```cpp
reactor_.run_for(timeout);   // the only place that ever blocks
```

Most of `run()`'s life is spent asleep on that line. Asleep on what? Woken by
what? Chapter 2's sequence diagram said "WSAPoll reports readiness," but
behind "reports" sits a question that must be answered — **how does a
sleeping thread get woken at all?** This chapter opens `src/reactor.cpp`,
where you will meet the book's two best bugs: not teaching cases we invented,
but real commits in this repository's history, caught by tests.

## Design: A Table and a Pipe You Can Poke

The reactor has three members that matter:

```cpp
mutable std::mutex mutex_;
std::unordered_map<SOCKET, descriptor_state> descriptors_;  // the table
SOCKET interrupter_ = INVALID_SOCKET;                        // self-connected UDP
std::atomic<bool> interrupted_{false};                       // dedup flag
```

The **descriptor table** is the roster: one row per socket, recording who is
waiting to read and write on it:

```cpp
struct descriptor_state
{
  std::function<void()> read_handler;    // invoked on read-ready (it is do_read)
  std::function<void()> write_handler;   // invoked on write-ready (it is do_write)
  bool empty() const { return !read_handler && !write_handler; }
};
```

What is stored here is the socket's **internal callback**
(`[this]() { do_read(); }`), not the user's handler — the user handler waits
until `do_read` has done the real system call and the result is `post`ed
(contract one, chapter 2). One registration per direction, matching the
socket's one-operation-per-direction rule.

The **interrupter** is a wake-up socket. It is a UDP socket `connect`ed to its own
address — write one byte into it and it instantly becomes readable. Mixed
into the `WSAPoll` watch list, any thread that wants to wake a sleeping
poller writes one byte. asio uses a socket pair on POSIX, eventfd on Linux
where available, a socket pair on Windows — sasio uses a self-connected UDP
socket, saving a pair of handles, same effect.

Two invariants, stated now and returned to repeatedly:

1. **The poll set is a snapshot.** The descriptor list `WSAPoll` sees is
   copied from the table at call time; anything registered after the copy is
   invisible for this round. So **any new registration that changes the wait
   set must wake a sleeping thread**, forcing a fresh copy;
2. **A ready registration leaves the table before it executes.** `WSAPoll` is
   level-triggered: as long as the data is unread, the next poll reports the
   same descriptor again. So the ready callback is moved out of the table
   before it runs — if `do_read`'s `recv` hits a false alarm (spurious
   readiness), it registers itself back.

## The Life of One Wake-up

`post` is the most common wake-up path. From `scheduler::post` to the sleeper
waking, five steps:

1. Thread B pushes the handler into the queue, releases the lock, and calls
   `reactor_.interrupt()`;
2. `interrupt()` first checks the atomic `interrupted_`: already true means
   someone just poked and the byte is in flight — return immediately
   (**dedup** — a thousand posts write one byte);
3. The flag flips false→true, and one byte is `send` into the interrupter;
4. Thread A is asleep in `WSAPoll`, whose list has the interrupter as its
   last item; the byte arrives and poll returns;
5. After handling ready descriptors, A **drains** the interrupter's buffer
   (a `recv` loop until empty) and clears the flag — the next sleep waits
   for the next poke.

The dedup flag is the clever part: waking is a **level** fact (a byte in the
buffer means readable), while `post` is an **edge** event (one poke per
handler). Without the flag, the drain logic and the write logic would race;
with it, many posts collapse into one wake-up — after waking, the loop top
re-reads the **whole** queue anyway, so one poke and ten pokes are the same.

## Case Study: Two Missed Wake-ups, One Correct from the Start

The three wake-up paths (post, first descriptor registration, new earliest
timer) do not share an origin. One was correct from the first version; two
were caught by tests. Understanding the set teaches the full price of the
snapshot model.

**Born right: post.** `scheduler::post` has called `reactor_.interrupt()`
unconditionally since the first version — the queue changed, the sleeping
thread must wake and look. It was never missing, because it sits in the same
function as the queue change, visibly coupled. The lesson: **wherever you
change state, wake people up right there**.

**Missed wake-up one: first descriptor registration.** When chapter 2 added
the descriptor table, `start_read` was as follows:

```cpp
void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  std::lock_guard<std::mutex> lock(mutex_);
  descriptors_[descriptor].read_handler = std::move(handler);
  // no wake-up. The sleeping thread knows nothing of this.
}
```

Single-threaded, flawless — the thread initiating `async_read_some` is the
run loop itself, and after registering it re-copies the snapshot next pass.
Multi-threaded, it breaks: thread A sleeps in `WSAPoll` (no timers, infinite
timeout) while thread B initiates `async_read_some` on a new socket — the
registration lands in the table, but A holds a stale snapshot, and no timer
or post will remedy it. **That read never completes.**

asio's `select_reactor` wrote the answer in `start_unready_op`:

```cpp
bool first = op_queue_[op_type].enqueue_operation(descriptor, op);
scheduler_.work_started();
if (first)
  interrupter_.interrupt();     // ← what sasio was missing
```

sasio's fix (`src/reactor.cpp`) copies the shape:

```cpp
void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  bool first = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_state& state = descriptors_[descriptor];
    first = !state.read_handler;      // empty→non-empty in this direction = first
    state.read_handler = std::move(handler);
  }

  // A thread already blocked in WSAPoll() is waiting on a snapshot of the
  // descriptor set taken before this registration, so it must be woken up to
  // rebuild that set. asio does exactly this in
  // select_reactor::start_unready_op():
  //   if (first) interrupter_.interrupt();
  if (first)
    interrupt();
}
```

The `first` test happens under the lock (empty→non-empty in that direction);
`interrupt()` runs outside it — reversed, you would make a system call while
holding the lock, for no reason. `start_write` handles its direction the same
way. The regression test `tcp_multithread_wakeup_test` discriminates
directly: one thread sleeps in `WSAPoll`, another registers a read while the
peer writes data, and the test asserts the handler ran within 3 seconds —
without the wake-up, the only wake source is the 5-second watchdog, and the
test fails.

**Missed wake-up two: the new earliest timer.** With descriptors fixed, one
sibling remained. Chapter 4's sleep formula is "next timer expiry minus now"
— and that timeout **is also a snapshot**: computed, then an earlier timer
arrives from another thread, and the sleeper still wakes at the old moment.
The worse variant: the formula said "sleep forever" (no timers existed), then
another thread calls `async_wait` for a 100ms timer — nobody wakes the
sleeper, and the sleep has no end.

asio answers in `select_reactor::schedule_timer`:

```cpp
bool earliest = queue.enqueue_timer(time, timer, op);
if (earliest)
  interrupter_.interrupt();     // new earliest timer → wake and recompute
```

sasio's fix makes `timer_queue::add` return whether the new entry became the
heap top, and `steady_timer::async_wait` calls `scheduler::wake_waiters()`
(inside: `reactor_.interrupt()`) accordingly. The regression test
`timer_multithread_wakeup_test`: the run thread sleeps with no timers, the
main thread arms a 100ms wait — before the fix the handler never runs, and
the test fails on a `fired` flag rather than hanging (the main thread calls
`stop()` after one second to end it).

**Three comparisons, one pattern.** post's wake-up is "change state, wake
right there"; the two misses are both "a new waiting source" (new descriptor,
new timer), fixed by "became the new earliest waiter → wake." The whole
snapshot model reduces to: **anything that would change `WSAPoll`'s answer
must wake whoever is waiting for that answer.** asio's `epoll_reactor` never
has the first miss — epoll's descriptor set lives in the kernel, so
`epoll_wait` sees new registrations naturally; but the timer miss still
exists on epoll without timerfd (`update_timeout()` → `interrupt()`).
Platform mechanisms differ; the snapshot trap is shared.

## Close Reading: run_for in Four Movements

The receiving end is `run_for`. About 100 lines total, four movements:

```cpp
void reactor::run_for(clock::duration timeout)
{
  std::vector<WSAPOLLFD> poll_fds;        // ① copy the snapshot under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (each row of the descriptor table)
      if (read or write registered)
        { poll_fds.push_back(fd); remember socket; }
    poll_fds.push_back(interrupter);      // the truncheon rides last
  }                                       // ② lock released — poll without it

  const int result = ::WSAPoll(poll_fds.data(), ..., to_milliseconds(timeout));

  std::vector<std::function<void()>> ready_handlers;   // ③ move out under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (each ready descriptor)
      move that direction's callback out of the table into ready_handlers;
  }
  for (auto& handler : ready_handlers)    // ④ execute without the lock
    handler();

  if (interrupter readable) { drain buffer; clear flag; }  // ⑤ tidy up
}
```

Four details, each worth its own paragraph:

- **② No lock during poll**. `WSAPoll` can sleep forever; sleeping with the
  reactor's lock held deadlocks every `start_read` — the classic recipe
  (whoever needs the lock wants to wake you; you hold the lock waiting to be
  woken). The snapshot is copied under lock, poll runs without it, and the
  window between the two is exactly why wake-ups are needed;
- **③ Move out, then run**. Level-triggered poll only looks at buffers; if
  the ready callback stayed in the table and `recv` ran late, the next poll
  reports the same descriptor again. Moving the callback out (invariant 2)
  blocks repeats and false alarms alike — if `do_read` really reads
  `would_block`, it registers itself back;
- **④ Execute without the lock**. The same iron rule as the scheduler's
  "dequeue under lock, execute outside": callbacks re-register, they post —
  all of it may need the lock;
- **⑤ Drain your own**. With several threads sleeping in their own `WSAPoll`
  calls, one byte may wake them all; the first to wake drains the buffer and
  the others find `recv` empty, clear the flag, and re-sleep. A few spurious
  wakes are harmless — the queue is the source of truth; the wake-up is a
  doorbell, not a courier.

Error handling has its own note: `WSAEINTR` returns directly (an interrupted
system call is not a failure), anything else throws `std::system_error` —
the teaching library does not swallow errors.

## Comparison Table

| Question | asio (select backend) | asio (epoll backend) | sasio |
| --- | --- | --- | --- |
| Waiting call | `select()` (WSAPoll on Windows without IOCP) | `epoll_wait` | `WSAPoll` |
| Descriptor set | user-space snapshot | kernel-side, registration is instantly visible | user-space snapshot |
| Wake on new registration | yes (`start_unready_op`) | no (epoll sees it naturally) | yes (`if (first)`) |
| Wake on new earliest timer | yes (`schedule_timer`) | no with timerfd / otherwise yes (`update_timeout`) | yes (`add` returns earliest) |
| Interrupter | socket pair / eventfd | eventfd / socket pair | self-connected UDP socket |
| Source entry | `detail/impl/select_reactor.ipp` | `detail/impl/epoll_reactor.ipp` | `src/reactor.cpp` |

One sentence: **all of the snapshot model's complexity goes into staying
awake** — epoll moved the descriptor set into the kernel and saved exactly
this part; every step of wake-up logic on the select/WSAPoll route is
isomorphic between asio and sasio.

## Exercises

**Experiment**: reproduce the first miss by hand. Open `src/reactor.cpp`,
comment out the two `if (first) interrupt();` calls in `start_read` and
`start_write`, rebuild, and run `test_ip_tcp.exe` alone:

```text
cmake --build build --target test_ip_tcp
build\test_ip_tcp.exe
```

Watch `tcp_multithread_wakeup_test` fail after about 5 seconds — the handler
did run, but 5 seconds late (the watchdog woke it). Restore the two lines and
the test passes instantly. You have just verified the snapshot model's price:
one missing wake-up line degrades a millisecond delivery into "wait for the
next accident."

**Bug hunt**: `do_read()` in `src/ip_tcp.cpp`, when `recv` returns
`would_block`, calls `reactor_.start_read(...)` to register itself again
instead of reporting an error. Answer first: poll said "readable" — how could
`recv` read nothing? Then answer: if it did not re-register, what state would
this read be in? (Hint: the callback has already been moved out of the table —
invariant 2.)

---

Readiness, wake-ups, the descriptor table — all in place, and one of the
three wake sources remains: time. Chapter 4's loop sleeps until "the next
timer expires" — where does that "next" come from? How the min-heap stays
consistent, how cancellation never re-enters, and a heap out-of-bounds bug
caught by asio's own test suite. Next chapter: timer_queue.
