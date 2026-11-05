<!-- translated-from: book/src/ch02-model.md sha256:edc7a0b2d266 -->

# Reactor and Proactor: The Full Life of One Async Operation

Start from the code you know best:

```cpp
char buf[1024];
int n = recv(sock, buf, sizeof(buf), 0);
```

When `recv` returns, the result is already in your hands: either you have data,
the peer closed, or something failed. Function return is a full stop. That is
synchronous.

The problem only shows up once you have two waits. Write a TCP server where
one thread must sit in `accept` waiting for new connections *and* sit in
`recv` waiting for data on an established connection — whichever blocks first
starves the other. Two full stops, one execution path; one of them is going
to hang.

The obvious escape is more threads: one per connection, each owning its own
`recv`, nobody blocking anybody. It works, and it is genuinely the easiest to
write. The bill arrives when connection counts climb — a stack per thread,
scheduler churn, and the vast majority of threads sitting idle 99% of the
time. At thousands of connections, the thread-per-connection design collapses
before your server does.

Another way is to concentrate the waiting: instead of every call site
blocking on its own, **one thread watches a large set of descriptors and
handles whichever is ready**. The waiting moves out of each individual `recv`
and into a single system call. That is a reactor.

A third way goes further and hands off the reading and writing too. At
initiation you only say "call me when it is readable"; the kernel moves the
data into your buffer, and the framework hands you the finished result. That
is a proactor.

All three compress into one sentence: **synchronous code does the work itself
and continues with the result in hand; a reactor receives "you may act now";
a proactor receives "it is done."** Everything else in this chapter unfolds
that last clause in code.

## Two Faces

**reactor**: the kernel only answers "which descriptors are ready"; you still
perform the `read`/`write` yourself. select, poll, epoll, kqueue — and
WSAPoll on Windows — all belong to this family. What you get is a
**readiness notification**.

**proactor**: the kernel performs the I/O itself. You submit the buffer at
initiation, and the callback receives how many bytes were transferred.
Windows IOCP (I/O Completion Port) is the textbook implementation. What you
get is a **completion notification**.

asio has both faces and switches by platform:

- **Windows**: IOCP, a true proactor. At initiation the OVERLAPPED structure
  and buffer go to the kernel together; what comes out of the completion port
  queue is finished work;
- **POSIX**: epoll/kqueue/select are reactors at heart, and asio uses them to
  **simulate** a proactor — register interest in readiness, poll reports
  ready, the framework performs the system call itself, then wraps the result
  as a "completion" delivered to the handler. The key to the simulation is
  that last step: what the user sees is still "the result arrived," never
  "you may act now."

sasio, on Windows, deliberately does not use IOCP; it takes the simulation
route with WSAPoll as its reactor. The reason is practical: IOCP's completion
ports, OVERLAPPED structures, and `GetQueuedCompletionStatus` bury the model
inside a full set of Win32 machinery — you have to climb Windows before you
can see the asynchrony. The WSAPoll version keeps every line on socket
semantics, and it corresponds structure-for-structure with asio's POSIX
backends (`epoll_reactor` / `select_reactor`) — same layering, same
responsibilities, an order of magnitude less code. The price is that this
book skips a true proactor; chapter 14's reading map gives the
`win_iocp_io_context` catch-up path.

Whichever face, the API asio shows you is identical: `async_read_some`
behaves the same on both platforms. That fact — "API constant, face changes"
— proves the **engine contracts** can exist independently of platform.
Here are the three sentences the whole book stands on.

## The Engine Contracts (Three Invariants)

**Contract one: an initiating function never executes the handler inline.**
I/O completions (read/write/connect/accept/resolve) always go through
`scheduler::post` into the queue; expired timers are invoked by the scheduler
directly inside the run loop (also not in the initiating function);
`dispatch` executes inline only when "this thread is already processing this
context" — it is the only legitimate exception, and its condition is
testable. What an initiating function does is one thing: register what should
happen when readiness arrives. (Chapter 10's `strand::dispatch` replays the
same principle at the serialisation layer: the criterion becomes "this strand"
rather than "this context," but it equally requires the calling thread to
already be inside `run()` — matching asio's
`can_dispatch() && !locked_` in `strand_service::do_dispatch` exactly.)

**Contract two: handlers always execute inside `run()`.** When the initiating
function returns, your program continues on its way; the result is delivered
at some future moment, by some thread that called `run()`. Without `run()`,
no handler ever executes — even if the data arrived long ago. Every
submission path in sasio obeys this, strand included (chapter 10). This is
the classic beginner trap: post and forget `run()`, and the code sits there
quietly doing nothing.

**Contract three: notifications come from exactly three sources.** The
handler queue (things posted in), ready descriptors (reported by the
reactor), and expired timers (the heap's top). Every pass of the engine loop
re-checks all three sources and handles whichever has work. Multi-threaded
races, wake-ups, and the stopping rule all orbit these three sources.

Memorize the three sentences. The work count in chapter 4, the wake-ups in
chapter 5, and cancellation in chapter 6 are all houses built on this
foundation; when any behavior surprises you later, come back and reread the
contracts — you can usually derive the answer yourself.

## The Life of One `async_read_some`

The most common chain. Single thread, socket connected, the peer may send at
any moment:

```text
 user code                socket                  reactor                scheduler
    | async_read_some(buf,h)  |                       |                       |
    |------------------------->| store buf/h, register |                       |
    |                         | read interest         |                       |
    |                         |---------------------->| start_read(fd)        |
    |   returns (nothing happened yet)                 |                       |
    |<------------------------|                        |                       |
    |                         |                        |  WSAPoll(all fds)     |
    |                         |                        |== peer data arrives ==|
    |                         |  do_read(): recv()     |                       |
    |                         |<----------------------| invokes the registered |
    |                         |  got n bytes           | callback              |
    |                         |-------- post(h(n,ec)) --------->               | enqueue
    |                         |                        |                       |
    |     h(ec, n) runs inside the run() loop           |                       |
    |<--------------------------------------------------------- ------------|
```

There are three hand-offs on this chain — a single-threaded program, so three
points in time, not three threads:

1. **Initiation**: `async_read_some` stores the buffer and handler in the
   socket object and registers read interest for the descriptor with the
   reactor, then returns. It has not touched any data — the peer may not
   have sent anything yet;
2. **Readiness**: the `run()` loop is blocked in `WSAPoll`. The peer's data
   arrives, poll returns, the reactor finds the callback registered for this
   descriptor and invokes it — and that callback is not your handler; it is
   the socket's own `do_read`;
3. **Delivery**: `do_read` performs the real `recv`, gets the byte count, and
   `post`s your handler together with the result into the scheduler's queue.
   The next pass of the run loop takes it out and executes it.

Note the division of labor between steps 2 and 3: **the reactor only wakes
people up; the real I/O is done by the socket's `do_read`**. That is the
bridge of "simulating a proactor with a reactor" — after the wake-up, add the
system call, and what the user receives is still a completion notification.
Contract one is tested twice on this chain: no handler ran at initiation, and
none ran after `do_read` (it only posted) — your handler appears only at
step 4.

Multi-threaded `run()` introduces races on this chain: two threads both
asleep in `WSAPoll` — who gets woken, and can readiness be delivered twice?
That is chapter 5's topic, and the richest bug mine in the book.

## Close Reading: The Skeleton of the Engine Loop

That chain all converges into one loop. `run_one_impl` in
`src/scheduler.cpp` (83 lines) is the most important stretch of code in the
library; here is the skeleton first — locks and counting stripped, shape
kept:

```cpp
for (;;)
{
  if (stopped())                        // someone called stop(): quit now
    return false;

  if (!queue_.empty())                  // source 1: the handler queue
    { pop one and invoke it; return true; }

  if (earliest timer expired)           // source 2: the timer heap
    { pop and invoke its handler; return true; }

  timeout = next timer expiry;          // source 3 decides how long we may sleep
  reactor_.run_for(timeout);            // sleep in WSAPoll until any source fires
}
```

Three lines land the three contracts: the execution before each `return true`
is contract two (everything happens in the run loop); the split between
source 1 and source 2 — queue handlers were "posted by someone," timer
handlers are invoked by the scheduler directly — is contract one's "not
through the queue"; the last two lines are contract three's third source, and
the only place that ever blocks.

One detail deserves its own paragraph: after `run_for` returns, control goes
back to the top of the loop and **looks at the queue again**. Why? Because
during the sleep, any thread may have `post`ed a new handler — miss it and
that handler waits for nothing. The whole engine's shape is that one
sentence: **re-check every source, every pass**. Chapter 5 covers what asio
and sasio each pay to make sure "a thread asleep in poll gets woken when
another thread posts"; for now, just keep the shape.

Three details were erased from the skeleton — how the work count decides
"anything left to do," where un-executed handlers go after `stopped()`, and
in what order locks are taken so nothing deadlocks — all of it filled in when
chapter 4 reads the full text.

## Comparison Table

| Question | asio (Windows face) | asio (POSIX face) | sasio |
| --- | --- | --- | --- |
| Waiting mechanism | IOCP completion port | epoll / kqueue / select | WSAPoll |
| Who performs I/O | kernel (true proactor) | framework, after readiness | framework, after readiness |
| Source entry | `detail/win_iocp_io_context.hpp` | `detail/epoll_reactor.hpp`, `detail/select_reactor.hpp` | `src/reactor.cpp` |
| Timer drive | completion-port wait timeout | reactor computes poll timeout | scheduler computes poll timeout |
| Relationship | — | isomorphic to sasio | — |

One sentence: **sasio takes asio's POSIX route and runs it on Windows**.
Reading sasio's reactor is reading a miniature of asio's `epoll_reactor`;
afterwards, the POSIX face of asio needs no mental gear change. The true
proactor (IOCP face) differences wait for chapter 14.

## Exercises

**Experiment**: build and run the echo server:

```bat
cmake --build build --target ex03_echo_server
build\ex03_echo_server.exe 5555
```

Open two more terminals, `telnet 127.0.0.1 5555` from each, and type in
alternation — both connections get their echo. A single `run()` thread is
serving two connections at once; explain it against the sequence diagram:
both sockets are registered in the same descriptor table, one `WSAPoll`
reports both ready, and the loop handles whoever is ready. Try connecting one
client and sending nothing: the other's echo is unaffected, because the idle
descriptor never becomes ready and the loop wastes no system call on it.

**Bug hunt**: open `tests/asio/steady_timer.cpp` and find the cancel case:
`expires_after` + `async_wait`, then `cancel()`. The return value is 1 (one
wait cancelled), yet the handler **is still invoked** — with
`operation_aborted`. Predict two things first: at what moment does the
handler run, and what would go wrong if it ran synchronously inside
`cancel()`'s call stack? Then run and check. "Cancellation does not mean no
notification" is chapter 6's core contract, and you can probably already
guess the reason (reentrancy).

---

The model stands: initiation only registers, completion happens in `run()`,
notifications come from three sources. But "initiation only registers"
presumes somewhere to register. Where does the socket's state live, who owns
the reactor and the timer heap, and why does `io_context` itself contain
almost no logic? Next chapter: the service registry.
