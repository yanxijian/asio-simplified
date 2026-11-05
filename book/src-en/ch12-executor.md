<!-- translated-from: book/src/ch12-executor.md sha256:3662da51bc7b -->

# The Executor Layer: Completion-Delivery Policy

Chapter 10 left a hook: sasio's strand serializes **the handlers you
explicitly give it**, but the intermediate completions inside `async_write`
do not follow automatically — in asio that is called "automatic inheritance,"
and it is the executor layer's work. This chapter covers the cut layer: what
problem it solves, what its mechanism looks like, and the minimum set you
would install to bring it back to sasio.

Unlike the previous eleven chapters, this chapter's "close reading" reads
real asio symbols — sasio has no counterpart, which is exactly the Part V
reading mode: understand the concept against asio's implementation, using
sasio's absence as the contrast.

## The Problem: Delivery Policy Is a First-Class Need

Callback-style async has a question we have quietly skipped: on **which
thread, and in what manner**, does the handler execute? The engine chapters'
answer was always plain — "in a `run()` thread, taken from the queue." Fine
for single-threaded; with multi-threaded `run()`, it stops being enough.
Three real scenarios:

1. **Serialization**: one session's read and write handlers must not run
   concurrently — that is strand, but every intermediate completion of a
   composed operation should enter the strand too, or the lock gets bypassed;
2. **Offloading**: parsing the resolved data should not occupy the I/O
   thread — you want a handler to "run elsewhere" and come back;
3. **Generalization**: with every new execution venue (thread pool, strand,
   custom scheduler), the socket type would need instantiating once more —
   a type explosion.

Generalizing the three: **"where and how to run" must become a first-class
property of the handler**, not a rule hardwired into the engine. That
property is the executor.

## Design: The Framework in Three Sentences

asio's executor framework compresses into three sentences:

1. **Every handler has an associated executor**. Where from? A two-level
   lookup chain: if the handler type carries its own (`bind_executor(strand,
   h)` produces a bound object), use it; otherwise use the default passed by
   the caller — and I/O objects pass their own executor as that default,
   which is by default an `any_io_executor` wrapping whatever they were
   constructed with (usually the io_context's);
2. **Three delivery actions**: `post` (never inline, always queued),
   `dispatch` (possibly inline — chapter 10's two criteria are its
   implementation), `defer` (like post, but when the completion chain is a
   continuation "just handed down," it does not jump the queue — giving the
   engine a batching opportunity);
3. **A strand is an executor adaptor**. `strand<Executor>` wraps an inner
   executor; `post`/`dispatch` still go to the inner one, with queueing and
   serialisation added — so strand-over-thread-pool and strand-over-strand
   both compose.

sasio has an incomplete version of 1 (`thread_local current_scheduler` plays the
"fallback" role), half of 2 (post/dispatch, no defer), and an object version
of 3 (a strand bound to a context rather than adapting an executor). What is
missing is exactly the half that "makes the policy a parameter."

## Trace: One Binding, Propagated

The executor's value is clearest in chapter 8's composed operations. The
delivery chain of this line of asio:

```cpp
asio::async_read(socket, asio::buffer(buf),
    asio::bind_executor(strand, handler));
```

`bind_executor` produces a bound object: handler and strand packaged.
Afterwards:

1. `async_read` internally builds a `read_op`, whose final completion
   handler is that bound object;
2. Every **intermediate completion** (one segment of `read_op` finishing and
   initiating the next) asks the current handler's associated executor when
   delivering to the engine — and the bound object carries the strand;
3. So every intermediate completion also queues through the strand, and
   **the whole chain is serialized**.

Initiate once at step 1, effective layer by layer at step 2 — the mechanism
of "bind once, effective everywhere" is that **the lookup chain travels with
the handler**, not with "who is calling." sasio's ex04 hand-wrap covers only
the outermost lambda, and the inner completions leak out — because sasio's
handlers are bare `std::function`, with nowhere to hang "my executor is
this."

Pin the three delivery actions to scenarios: I/O completions take post (no
inline — you are on the reactor callback's stack, reentrancy is dangerous);
a user calling `dispatch` inside a handler may go inline (already on a
handler's stack, saves one delivery); a completion chain's continuation
takes defer (not inline, but tells the engine "I am a continuation" so it may
run it immediately instead of queueing behind — a cache-locality
opportunity).

## Close Reading: The Lookup Chain and Type Erasure

Two asio symbols carry the whole layer. The first, the **lookup chain's
implementation**, in `asio/associated_executor.hpp`:

```cpp
template <typename T, typename Executor = inline_executor>
struct associated_executor
{
  using type = typename detail::associated_executor_impl<T, Executor>::type;

  static type get(const T& t, const Executor& d = Executor())
  {
    return detail::associated_executor_impl<T, Executor>::get(t, d);
  }
};
```

`detail::associated_executor_impl` is a two-level test: if T has its own
`executor_type`, use it (the specialization on `executor_type`); if T
directly specializes `associated_executor`, use that (the `void_t`
specialization); otherwise fall back to the caller-supplied default `d`
(the primary template). Note the chain has only two levels — "the I/O
object's executor" is not a level of the chain; it is **the default argument
passed in by the caller**: the socket supplies its own executor as `d` when
initiating. This "trait + fallback" shape appears everywhere in asio —
associated_allocator and associated_cancellation_slot are the same. When you
see "layers of traits" while reading asio, this is the skeleton.

The second is **type erasure**, `asio/any_io_executor.hpp`:

```cpp
class any_io_executor :
  public execution::any_executor<
      execution::context_as_t<execution_context&>,
      execution::blocking_t::never_t,
      execution::prefer_only<execution::blocking_t::possibly_t>,
      execution::prefer_only<execution::outstanding_work_t::tracked_t>,
      execution::prefer_only<execution::outstanding_work_t::untracked_t>,
      execution::prefer_only<execution::relationship_t::fork_t>,
      execution::prefer_only<execution::relationship_t::continuation_t>
    >
```

Note it is a **class** inheriting `execution::any_executor<...>` (the
property names use the nested form `blocking_t::never_t`), seven properties
turning "must not block," "work accounting," and "continuation relations"
into queryable/modifiable keys. It is an erasure container accepting any
executor satisfying those properties: small-object-optimized storage inside,
virtual dispatch on call. Since 1.74 it is the default executor parameter of
every I/O object — one `basic_stream_socket<tcp, any_io_executor>` type
covers io_context, thread_pool, and strand. The cost is stated openly: one
extra indirection per delivery, plus a heap allocation when the small-object
buffer does not fit. asio traded template explosion for it; sasio, with one
io_context, does not need the layer at all.

## Three Columns: Absent ↔ Implementation ↔ Where to Find It

| Capability | sasio today | asio implementation | where in asio |
| --- | --- | --- | --- |
| Deliver completions to a chosen venue | none (handlers are bare `std::function`, fixed delivery to the scheduler) | associated executor lookup chain + `bind_executor` | `asio/associated_executor.hpp`, `asio/bind_executor.hpp` |
| strand serialization | yes (an object bound to the context) | an executor adaptor, composable | `asio/strand.hpp`, `detail/impl/strand_service.ipp` |
| Offload to a thread pool | none | `thread_pool` (itself an executor) | `asio/thread_pool.hpp` |
| Runtime choice of venue | none (one io_context) | `any_io_executor` type erasure | `asio/any_io_executor.hpp` |
| Execution properties (never block, continuation batching) | none | the `execution::` property framework | `asio/execution/*.hpp` |
| Continuation delivery (defer) | none | `defer` + continuation semantics | `asio/defer.hpp` |
| Inline delivery | yes (two criteria, ch10) | `can_dispatch()` + `dispatch` | `detail/impl/scheduler.ipp` |

For every row where "sasio today" says "yes," the implementation was
dissected in earlier chapters — the engine's two rows (strand, dispatch) are
exactly the parts of the executor framework that **work without
parameterization**. That, in reverse, explains sasio's trade: the engine
trunk (ch3–11) is self-consistent without executors; everything that needs
them is generalization.

## Exercises

**Experiment**: use ex04's hand wrap to prove "no automatic inheritance."
Build a two-hop scenario on top of ex04:

```cpp
auto bound = bind_strand(strand_, [&](error_code) {
  std::puts("first hop: strand=1");
  const auto hop1_thread = std::this_thread::get_id();

  timer_.async_wait([&](error_code) {      // hop two: unwrapped
    const bool in_strand = strand_.running_in_this_thread();
    const bool same_thread = std::this_thread::get_id() == hop1_thread;
    std::printf("second hop: strand=%d, same_thread=%d\n",
        in_strand ? 1 : 0, same_thread ? 1 : 0);
  });
});
timer_.async_wait(bound);
```

Predict: what is `running_in_this_thread()` on hop one? On hop two? Run and
compare — hop two has left the strand, because your hand wrap covered only
hop one's completion. That is the negative textbook of "bind once, effective
everywhere," and the reason asio built the lookup chain into the handler
system.

**Bug hunt**: open `asio/strand.hpp` and count the adapter's interface
surface: beyond the four delivery members (`post` / `dispatch` / `defer` /
`running_in_this_thread`), there are `query` / `require` / `prefer` — the
executor property protocol's trio. List what `sasio::strand` lacks against
these four members, then answer: without `query`, which asio code fails to
port? (Hint: code like
`asio::query(s, execution::context)` that asks the strand for its underlying
context.)

---

The executor answers "where to run." The other half of the layer remains —
"how the result comes back." The completion token and its `async_result`
system are next chapter's dissection.
