<!-- translated-from: book/src/ch01-why.md sha256:e79e19e69773 -->

# Why Read the 4,200-Line Version First

Open the source of [chriskohlhoff/asio](https://github.com/chriskohlhoff/asio),
version 1.38.2: the `include/asio` directory holds 600 `.hpp` headers and
169,063 lines (about 197,000 if you count the `.ipp` implementation files as
well). Before that, you have most likely only used its API:

```cpp
asio::io_context io_context;
asio::ip::tcp::socket socket(io_context);
socket.async_read_some(buffer, handler);
io_context.run();
```

Four lines, and intuitively not hard. But the moment you ask "what is `run()`
actually running," or "who is the handler, on which thread, and when does it
fire," following `async_read_some` leads to a first screen of
`basic_stream_socket` template declarations — parameterized on protocol and
executor; below that sits the implementation, past
`detail/reactive_socket_recv_op` operation objects, past the associated
executor lookup chain, with six platform I/O backends (epoll, kqueue, select,
dev_poll, IOCP, io_uring — plus the `null_reactor` and `spin_reactor` stubs)
waiting for you to figure out which one is active.

The problem is not that asio is badly written — it is an industrial library,
and those layers each earn their keep. The problem is that **the async model
you want to understand is tangled with the general-purpose machinery that
makes it portable and extensible**: the model itself is maybe two or three
thousand lines; everything else exists so the model runs on six platforms and
accepts arbitrary completion tokens.

So this book goes the other way around: first read the model inside a 4,200-line
implementation, then take that understanding to the real thing. The
implementation is **asio-simplified** (sasio for short) — the repository this
book ships with.

## How This Book Reads Code

sasio's trade-off is one sentence: **keep the engine, cut the composition
layer**. It has `io_context`, the handler queue, the reactor, the timer heap,
strand, composed operations, UDP, the resolver, and C++20 coroutines — the
whole trunk of asio. It has no executor framework, no completion tokens, no
platform branches (Windows + `WSAPoll` only), no IPv6. Every cut is
documented: why it was made and where to find the machinery in asio. The
comparison tables at the end of each chapter are that documentation in
summary form.

The method, which is also how to read any source code, has three steps:

1. **Trace one operation**: pick a complete call chain (say `async_read_some`
   from initiation to handler execution), walk it end to end, and note who
   calls whom and where the data lives;
2. **Change the code to verify**: for every guess the trace produces, change a
   line and run the tests. sasio is small enough that changing it costs
   nothing psychologically;
3. **Find the counterpart in asio**: once the model is clear in sasio, open
   the same-named file in asio — the class names and layering are aligned, so
   you will recognize every concept, just wearing template clothing.

Step 3 works because sasio is not "something like asio" — it is written to
asio's **design**: `io_context` is a service container, `scheduler` uses a
work count to decide when to stop, the reactor is woken by an interrupter.
The differences are deliberate, and every one of them is on the record.

The book has 14 chapters, each with a fixed six-part shape: why you would
need the component, its data structures and at most three invariants, a trace
of one operation's full life, a close reading of the most important stretch
of code, the comparison against asio, and one experiment plus one bug hunt.
You never need to memorize the shape — it is the same thinking exercise,
repeated.

## The Route

sasio's components form a chain, and the book follows it:

```text
io_context ── service registry (ch3)
    │
    ├─ scheduler ──── handler queue, work count (ch4)
    │
    ├─ reactor ────── WSAPoll, descriptor table, wake-ups (ch5)
    │
    ├─ timer_queue ── min-heap, timers (ch6)
    │
    ├─ socket/acceptor ── initiate and complete, two phases (ch7)
    │
    ├─ composed operations ── async_read / async_write (ch8)
    │
    ├─ UDP / resolver ── two reuses of one engine (ch9)
    │
    ├─ strand ─────── serialisation (ch10)
    │
    └─ awaitable ───── C++20 coroutines (ch11)
```

The first two chapters are not on the dependency chain, but the chain wobbles
without them: chapter 2 builds the reactor/proactor model and states the
three engine contracts the rest of the book leans on. Chapters 12 through 14
return to asio: how the two cut layers (executors and completion tokens)
work, and how to read the real thing.

Every component ships with tests from its own test suite — the 9 test files
in `tests/asio/` are ported from `asio/src/tests/unit/`, with assertions and
comments kept as close to the originals as possible. That means behavior you
verify on sasio is asio's behavior, not an approximation.

## The First Stretch of Code

Here is a complete sasio program — the body of
`examples/ex01_post.cpp`:

```cpp
sasio::io_context ctx;

ctx.post([] { std::puts("handler 1"); });
ctx.post([] { std::puts("handler 2"); });
ctx.post([] { std::puts("handler 3"); });

ctx.run();
```

`post` puts three handlers into a queue and returns. Not one of them has run
— post's promise is "enqueued," never "running." `run()` is the executor: it
takes handlers from the queue one at a time until the queue is empty, no
timers remain, and no descriptors are registered — no work left — and then
it returns. The program prints three lines and exits.

Three behaviors deserve a second look:

- When `post` returns, **none of the handlers has run**. Initiation and
  execution are separated; that dividing line runs through the whole book;
- `run()` returns only when there is **nothing left to do** — not just an
  empty queue, but also pending timers and registered descriptors
  (chapter 4 covers the exact test);
- Once `run()` returns for lack of work, the io_context is stopped, and it
  takes `restart()` to run it again. That is asio's behavior, copied.

A handful of lines, and all three protagonists are on stage: the **queue**
(where handlers wait), **work** (what keeps the loop alive), and **stopping**
(when it is over). They carry the first half of the book — chapters 3 and 4
each take one.

## sasio and asio, Side by Side

| | sasio | asio 1.38.2 |
| --- | --- | --- |
| Headers | 18 | 600 (`.hpp` under `include/asio`) |
| Lines | 4,207 (measured; `include/sasio` + `src` `.hpp`/`.cpp`) | 169,063 (excluding `.ipp`; about 197,000 with) |
| I/O backends | 1 (WSAPoll) | 6 (epoll / kqueue / select / dev_poll / IOCP / io_uring) |
| executors / completion tokens | none | yes, throughout the API |
| Tests | 9, ported from asio's own suite | hundreds |

This table is not "small beats big" — it is a **reading order**: nothing in
the 4,200 lines is machinery you can skip, and most of the 169,063 lines are.
Understand the former, and the latter becomes two problems — "recognize the
model you know" and "understand why each layer of machinery exists" — instead
of one impossible one.

## Exercises

**Experiment**: build sasio from scratch and run the whole suite. From the
repository root (Windows + MSVC):

```bat
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

All 9 tests should pass in about 10 seconds. If Ninja is missing, the default
generator works too — `cmake --build build` invokes MSBuild for you
(executables land in `build\Debug\` in that case).

**Bug hunt**: open `tests/asio/io_context.cpp` and find the exception section
of `io_context_test`: handlers that throw `int` are interleaved with normal
counting handlers, and the test loops `run()` inside try/catch. Predict three
things before running:

1. After the first handler throws, do the normal handlers queued **behind it**
   still run in that pass?
2. When the exception escapes `run()`, is `stopped()` true or false?
3. Why does the loop need exactly three rounds before `break`?

Write your predictions down. Chapter 4 dissects `run_one_impl`, and that is
when you check the answers — you will find you can already reverse-engineer
the implementation from the behavior.

---

The model part starts with the next chapter, and with a question that does not
look like a question: where exactly is the asynchrony? How did the full stop
at the end of `recv` get moved?
