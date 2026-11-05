<!-- translated-from: book/src/ch07-socket.md sha256:bd7c0e04fd3b -->

# socket: The Initiating Function Only Describes What Happens When Ready

The engine chapters gave you all the tools: the queue for ordering, the
reactor for readiness, the timer heap for time. From this chapter the engine
goes to work. Start with real code from `src/ip_tcp.cpp`, the initiating
function of `async_read_some`:

```cpp
void socket::start_read_some(mutable_buffer buffer,
    std::function<void(error_code, std::size_t)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor, 0);
    });
    return;
  }

  if (read_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started, 0);
    });
    return;
  }

  read_pending_ = true;
  read_buffer_ = buffer;
  read_handler_ = std::move(handler);
  reactor_.start_read(handle_, [this]() { do_read(); });
}
```

Read it three times and you will not find `recv`. The function does exactly
four things: check state, **store** the buffer and handler in members,
register a "call me when readable" interest, return. The real `recv` lives in
another function, `do_read`, which the reactor calls only when readiness is
reported.

This chapter opens both phases — initiation and completion. It is the shape
shared by all of asio's socket code; every `async_*` of
`basic_stream_socket` is as follows.

## Design: One Slot per Direction

The socket's state is small, but every member earns its place:

```cpp
SOCKET handle_ = INVALID_SOCKET;

bool read_pending_ = false;
std::function<void(error_code, std::size_t)> read_handler_;
mutable_buffer read_buffer_;
// a symmetric set for the write direction
```

Three invariants:

1. **At most one outstanding operation per direction**. `read_pending_` is
   the slot marker — a second read initiation receives a `post`ed
   `already_started` (note: still posted; contract one holds on error paths
   too). asio supports any number with an operation queue; sasio uses one
   slot — a difference of capacity, not shape;
2. **The read/write/accept initiating functions perform no data-plane system
   call**; connect is the exception — initiation makes one non-blocking
   connect attempt (there is no "readiness" for connect: it either succeeds
   immediately, fails immediately, or is in progress, and only the third case
   registers write interest);
3. **Delivery always goes through `post`**, including immediate failures —
   `bad_descriptor` is not thrown in place but queued, so the handler always
   executes inside `run()` (contract two).

One boundary must be named: **the buffer's and handler's lifetimes are the
initiator's responsibility**. `read_buffer_` stores a pointer and a length,
not a copy — while the operation is pending, the caller must keep the buffer
alive. asio is the same; it is the standing convention of C++ async I/O.

## The Two Phases of One Read

Chapter 2's sequence diagram, now in code. **Phase one (initiation)** is
`start_read_some` above, ending at
`reactor_.start_read(handle_, [this]() { do_read(); })`.

**Phase two (readiness)** is invoked by the reactor when `WSAPoll` returns:

```cpp
void socket::do_read()
{
  if (!read_pending_)
    return;

  error_code ec;
  const int result = detail::socket_ops::recv_socket(handle_,
      read_buffer_.data(), clamp_to_int(read_buffer_.size()), ec);

  if (result < 0)
  {
    if (ec == would_block)
    {
      // Spurious readiness: register again and wait.
      reactor_.start_read(handle_, [this]() { do_read(); });
      return;
    }

    complete_read(ec, 0);
    return;
  }

  complete_read(error_code(), static_cast<std::size_t>(result));
}
```

This is where `recv` actually runs. Three exits:

- **False alarm**: poll said readable but `recv` returned `would_block` (the
  data vanished between the check and the call, or poll's level-triggered
  semantics carry some fuzz). The response is to register again and keep
  waiting — invariant 2 of chapter 5 ("move out, then run") makes the explicit
  re-registration mandatory, otherwise this read would vanish from the
  reactor's world;
- **Real error**: `complete_read(ec, 0)` — clear the slot, unregister
  interest, `post` the handler with the error code;
- **Success**: `complete_read(empty error code, n)`, same path.

`complete_read` itself is about ten lines: clear `read_pending_`, unregister
interest, move the handler out of the member, `post`. Emptying the slot means
**the slot can be reused by the next initiation immediately** — the previous
handler may still be sitting in the queue unexecuted, but a new
`async_read_some` is already legal. The handler's life and the slot's life
decouple here.

## Two Asymmetries: connect and close

**connect has no single end — it has three states.** The initiating function
`start_connect` calls non-blocking `connect` directly:

- immediate success (common on loopback): `complete_connect(empty error)`;
- immediate failure (unreachable, and so on): `complete_connect(ec)`;
- `would_block` (in progress): register write readiness and wait for the
  kernel to finish the three-way handshake.

On the third path, `do_connect` writes no data; it does one thing —
`getsockopt(SO_ERROR)` reads the connection's final verdict, then
`complete_connect`. **"Writable" here means "the handshake finished or
failed," and the result is collected from SO_ERROR.** This is the standard
non-blocking connect idiom, the same shape as asio's
`detail/reactive_socket_connect_op`.

**close injects completions rather than revoking them**. `socket::close()`
first `cancel()`: for each pending direction it calls
`complete_xxx(operation_aborted)` — the handler is `post`ed with the
"cancellation" error code; only then does it unregister the descriptor and
close the handle. So after close you **will** receive a completion for every
pending operation, with error code `operation_aborted`. Chapter 6's
"cancellation does not mean no notification" repeats itself verbatim on
sockets, for the same reason: the stack calling `close()` may be the very
stack the handler wants to touch, and invoking in place is reentrancy.

The acceptor is a socket special case: `async_accept` registers read
readiness (a new connection = the listening socket is readable), and
`do_accept` calls `accept()`, sets the new handle non-blocking, and hands it
over via `peer->assign()`. Same shape as the read; no separate treatment
needed.

## Close Reading: cancel and complete_read

Read the two halves of "cancel" connected — the closing act of a socket's
life:

```cpp
void socket::cancel()
{
  if (!is_open())
    return;

  if (connect_pending_)
    complete_connect(operation_aborted);
  if (read_pending_)
    complete_read(operation_aborted, 0);
  if (write_pending_)
    complete_write(operation_aborted, 0);
}

void socket::complete_read(error_code ec, std::size_t bytes)
{
  read_pending_ = false;
  reactor_.stop_read(handle_);

  std::function<void(error_code, std::size_t)> handler =
      std::move(read_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec, bytes]() mutable {
    handler(ec, bytes);
  });
}
```

Three details:

- `complete_xxx` is the idempotent closing gate: clear the flag, unregister,
  **move the handler out of the member**, post. Afterwards no trace of the
  operation remains in the socket's members — destruction, close, and the
  next initiation are all safe;
- The post's lambda **captures the handler and results by value** — it
  references no socket member at all. That is why `close` can close the
  handle immediately after complete: the notification in flight is
  self-contained;
- `if (!handler) return;` guards the degenerate case of cancelling an empty
  slot — defensive programming; the normal path never reaches it.

Against asio: `basic_socket::cancel` walks `reactor.cancel_ops(descriptor)`,
and the reactor delivers every operation in that descriptor's queue with
`operation_aborted`. Different mechanism (queue vs slot), same behavior
(completion guaranteed, never reentrant).

## Comparison Table

| Question | asio | sasio |
| --- | --- | --- |
| Initiating function | `basic_stream_socket.hpp` `async_read_some` | `src/ip_tcp.cpp` `start_read_some` |
| Completion carrier | `detail/reactive_socket_recv_op.hpp` (operation object: handler/allocator/work unit) | member slot + `post` lambda capturing the handler |
| Outstanding count | a queue per direction | one slot per direction |
| connect | `reactive_socket_connect_op` + SO_ERROR | `start_connect` / `do_connect`, same flow |
| Cancellation | `reactor.cancel_ops`, delivered per op | `cancel()`, delivered per slot |
| Handle layer | `detail/socket_ops.hpp` + `impl/socket_ops.ipp` (5,140 lines) | `src/socket_ops.cpp` + header (259 lines) |

asio's operation object deserves one more sentence: it is the "state carrier
that lives until the event happens," bundling handler, allocator, work unit,
and completion results into one heap object. sasio substitutes a member slot
— the cost is one pending op per direction (invariant 1), the benefit is that
this chapter can focus on the two phases themselves. Chapter 8's composed
operations need "state that lives to the end of the chain" again, and there
you will see the shared_ptr relay.

## Exercises

**Experiment**: take ch2's echo server down to socket level. Based on
`examples/ex03_echo_server.cpp`'s `session::do_read` / `do_write_some`,
answer and verify: with two connections sending 1KB each, do the two
sessions' `do_read`s interleave inside the single-threaded `run()`? Print a
marker and a timestamp in both handlers to check. (Expected: yes — each
session's `async_read_some` completion is an independent queue item; whoever
is ready is handled.)

**Bug hunt**: after `socket::close()` but before the posted
`operation_aborted` handler executes, user code initiates a new
`async_read_some` on the same socket object. `handle_` is already
`INVALID_SOCKET` — which branch of `start_read_some` does it take? What error
code does the handler get? Is the order of the two notifications (the old
aborted, the new bad_descriptor) guaranteed? Derive the answer from the code,
then verify with a 20-line program.

---

One `async_read_some` rarely brings back everything — TCP is a byte stream,
and one `recv` returns however much the kernel feels like. Making "keep
calling read_some until the buffer is full" into a reusable operation that
chains itself is the next chapter's composed operation: `async_read`.
