# Executor and completion tokens: the composition layer

<!-- translated-from: docs/zh/executor-and-tokens.md sha256:163eaa9e7e6d -->

> **中文主文档**：[../zh/executor-and-tokens.md](../zh/executor-and-tokens.md)
>
> **Scope**: a concept note explaining why sasio omits these two layers and what
> that costs. It is not an API reference.

## Two independent questions

Asio's asynchronous engine (`io_context`, the scheduler, the reactor, the handler
queue) only decides *when* a handler runs after an operation completes. Two
orthogonal questions sit on top of it:

| Question | Mechanism | Typical API |
| --- | --- | --- |
| On which **execution agent** does the handler run? | executor framework | `associated_executor`, `bind_executor`, `strand` |
| How does the caller **receive the result**? | completion token | `async_result`, `use_future`, `use_awaitable`, `deferred` |

The engine decides *when*; these layers decide *where* and *in what form*.

## Executors: where completions are delivered

Every handler in Asio is associated with an executor, defaulting to the I/O
object's executor. `post`, `dispatch` and `defer` use that association. A
`strand` is really an **executor adaptor**:

```cpp
asio::async_read(socket, buffer,
    asio::bind_executor(strand, handler));   // the whole composed chain is serialised
```

The important part is "bind once, applies everywhere": every intermediate
handler inside a composed operation inherits the association, not just the final
callback. Above this sit the `execution::` properties (`require(blocking.never)`,
`prefer(relationship.continuation)`, `execution::allocator`), which express "must
not block", "this is a continuation" and "which allocator".

## Completion tokens: how results are obtained

The last parameter of every Asio initiating function is a token.
`async_result<Token, Signature>` maps "token + completion signature" to a return
type and an initiation strategy, so the same call can be written as:

- a callback: a function taking `(error_code, size_t)`
- a future: `asio::use_future`
- a coroutine: `asio::use_awaitable`
- a deferred operation: `asio::deferred`
- a discarded result: `asio::detached`, or `as_tuple` / `redirect_error` to
  reshape the signature

Its value is that the API does not have to be duplicated per result form, and
third parties can define their own tokens.

## `any_io_executor`: type erasure

`any_io_executor` is a type-erased container for anything that satisfies the
executor requirements (the default executor argument for I/O objects since
1.74). It lets `basic_stream_socket<tcp, any_io_executor>` accept an
`io_context`, a `thread_pool` or a `strand` without instantiating a new socket
type per combination. The cost is one extra indirection per dispatch and a
possible heap allocation.

## What sasio chose

| Capability | Asio | sasio |
| --- | --- | --- |
| Deliver completions to a chosen strand/thread pool | `bind_executor`, inherited by composed operations | only `strand::post`, wrapped by hand |
| One API for callbacks / futures / coroutines | completion tokens | callbacks plus dedicated `awaitable` adapters |
| Choose the execution agent at runtime | `any_io_executor` | not needed: there is one `io_context` |
| Execution properties | `execution::` | none |

The reason is not that these features are useless, but that they are
**generalisation infrastructure**: they turn "where" and "how" into parameters,
at the cost of Asio's hardest template and type-erasure machinery. For a
4,100-line teaching version, keeping the engine and dropping the composition
layer is the better trade.

## Approximating `bind_executor` by hand

[`examples/ex04_bind_executor.cpp`](../../examples/ex04_bind_executor.cpp)
shows what Asio's `bind_executor` looks like when done manually, with a ten-line
wrapper:

```cpp
std::function<void(error_code)> bind_strand(strand& s,
    std::function<void(error_code)> handler)
{
  return [&s, handler = std::move(handler)](error_code ec) {
    s.post([handler, ec]() mutable { handler(ec); });
  };
}
```

The example completes 24 timers on two `run()` threads, sends every completion
through the same strand, and prints `max_active=1`. It demonstrates "deliver the
completion to a chosen execution agent", but it does **not** give Asio's
automatic inheritance: the intermediate layers of a composed operation are not
routed through the wrapper, only the handler you wrapped.

## When it becomes worth adding

1. **Executor + `bind_executor`**: the first gap to close if you want to port
   Asio examples or third-party Asio code directly. About 250-350 lines, and it
   touches every completion point and every composed-operation hand-off.
2. **Completion tokens**: a few overloads for futures or a single coroutine entry
   point are cheap; doing it without being misleading approaches
   `async_result`, at 300-500 lines.
3. **`any_io_executor`**: only worth it once there is a `thread_pool` or several
   contexts and the agent must be chosen at runtime. With a single `io_context`
   it is pure overhead.

Until then, the README constraint still holds: with multiple `run()` threads a
completion handler runs on the thread that completed the operation; wrap it in a
`strand` yourself if it must be serialised.
