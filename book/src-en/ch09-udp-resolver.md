<!-- translated-from: book/src/ch09-udp-resolver.md sha256:a27814c9337f -->

# UDP and the resolver: Two Reuses of One Engine

With the engine's three pillars in place and TCP's two-phase shape standing,
the skeleton of a network library is complete. This chapter adds two daily
puzzle pieces, each carrying its own lesson:

**UDP** swaps the system call (`sendto` / `recvfrom`) while **barely touching
the engine** — the same descriptor table, the same `WSAPoll`, the same
"register, ready, execute, post." By the end you will have confirmed: the
reactor was protocol-agnostic from birth.

The **resolver** (name resolution) is the opposite: `getaddrinfo` is a
blocking call with no descriptor and no readiness, so the reactor's model
cannot hold it. asio's answer is a thread pool; sasio's is **one short-lived
thread per query**. It is the only component in the library with a thread,
and the best teaching material for "work units keep `run()` alive."

## UDP: Just a Different System Call

`include/sasio/ip/udp.hpp`'s socket, field by field against the TCP version:

```cpp
bool send_pending_ = false;
std::function<void(error_code, std::size_t)> send_handler_;
const_buffer send_buffer_;
endpoint send_destination_;              // the extra one: where to

bool receive_pending_ = false;
std::function<void(error_code, std::size_t)> receive_handler_;
mutable_buffer receive_buffer_;
endpoint* receive_sender_ = nullptr;     // the extra one: write back who sent it
```

The API has exactly four async entries: `async_send_to(buffer, destination,
handler)` and `async_receive_from(buffer, sender, handler)` (a connectionless
protocol has no "connection," so every send and receive carries the peer's
address; hence no connect/accept, and the slot count drops from three sets to
two).

The two-phase shape is isomorphic to TCP. Receive as the example:

- **Initiate** `start_receive_from`: store the buffer, store the `sender`
  pointer, register read readiness, return;
- **Ready** `do_receive`: one `recvfrom` takes a complete datagram, writes
  the sender's address back through `from_sockaddr` into
  `*receive_sender_`, then `complete_receive`;
- **Deliver**: the handler goes out via `post`, carrying the byte count and
  the sender's endpoint.

The error paths copy TCP too: re-register on false alarms, `complete_receive(ec, 0)`
on real errors, and `close()` first `complete(operation_aborted)` for pending
directions. Every conclusion from chapter 7 holds on UDP unchanged, without
one exception — that is the value of the reactor abstraction: **the protocol
changed; the engine did not**.

Two UDP-specific notes:

- **`receive_sender_` is a raw pointer**. While pending it must point at a
  valid `endpoint` — the same user responsibility as "the buffer must stay
  alive." What is written back on arrival is "where this datagram came
  from," not "who this socket is connected to" (UDP has no connection);
- **Datagrams have boundaries**. One `recvfrom` takes one complete datagram;
  a too-small buffer truncates it (asio's behavior drops the datagram with an
  error through `WSAEMSGSIZE` handling in `socket_ops.ipp`; sasio delivers
  the error to the handler — see appendix E). TCP's "read it full" logic does
  not apply — no stream, no use for composed operations.

## The resolver: Making a Blocking Call Async

`getaddrinfo("example.com", "80")` can take hundreds of milliseconds to
seconds (DNS lookup), and the thread is stuck the whole time. Put it in the
run loop and you stall every handler; put it in the reactor and there is no
solution — it has no descriptor. The only way out: **give it to another
thread and keep running**.

sasio's `resolver::start_resolve`, in full (the only thread code in the
library — worth reading whole):

```cpp
void resolver::start_resolve(const query& q,
    std::function<void(error_code, results)> handler)
{
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_flag_ = cancelled;               // cancel() touches the latest query only
  }

  scheduler* sched = &scheduler_;
  scheduler_.on_work_started();             // ← work unit #1

  try
  {
    std::thread([sched, q, cancelled, handler = std::move(handler)]() mutable {
      error_code ec;
      results resolved = do_resolve(q, ec); // blocking getaddrinfo happens here

      // Decide cancellation once, when the lookup has finished. A cancel()
      // that arrives after this point no longer changes the result, the same
      // way asio leaves an already-completed operation untouched.
      const bool aborted = cancelled->load(std::memory_order_acquire);

      sched->post(
          [sched, handler = std::move(handler), ec,
              resolved = std::move(resolved), aborted]() mutable {
            struct release
            {
              scheduler* target;
              ~release() { target->on_work_finished(); }   // ← the unit returns
            } guard = {sched};

            handler(aborted ? operation_aborted : ec, resolved);
          });
    }).detach();
  }
  catch (...)
  {
    scheduler_.on_work_finished();          // no thread, the unit returns now
    throw;
  }
}
```

Five points, in time order:

1. **The work unit exists before the thread**. `on_work_started()` runs
   **before** the `std::thread` constructor — from that instant,
   `work_count_ != 0` holds, and `run()` cannot stop for "queue empty, no
   timers, no descriptors." Without it, the thread is still resolving, the
   run loop has exited, the context is destroyed, and the thread's `post`
   dereferences a dangling scheduler;
2. **The thread is detached**. It captures only values (the scheduler
   pointer, the query, the flag, the handler) and needs no join. The price
   is one hard constraint: **the io_context must outlive every outstanding
   `async_resolve`** — the `sched->post(...)` inside the thread dereferences
   the scheduler pointer, which dangles once the context is destroyed;
3. **Cancellation is a sample, not a recall**. `cancel()` only flips the
   latest query's flag to true; the thread's `getaddrinfo` cannot be
   interrupted and must return on its own. `aborted` is sampled **in the
   thread, after the lookup finishes** — a `cancel()` after that moment no
   longer changes the result. This matches asio's "an already-completed
   operation is untouched by cancellation"; the sampling position *is* the
   semantics;
4. **The result returns to the run loop via `post`**. The thread never calls
   the handler directly — its result (error code + endpoint list) is
   captured by value into the lambda and delivered to the queue;
5. **The unit returns on the posted lambda**. The `release` guard's
   destructor fires **after** the handler runs — the unit stays on the books
   through parsing and queueing, released only when the handler finishes.
   Exceptions do not leak it either: the RAII old trick, `handler_work`'s
   cross-thread cousin from chapter 4.

The synchronous `resolve()` shares the same `do_resolve`, throwing
`std::system_error` on failure (the error code rides its own resolver
category, with messages from `gai_strerror`). It is a teaching convenience;
real code should use the async form.

## Comparison Table

| Question | asio | sasio |
| --- | --- | --- |
| Who resolves | a resident thread pool (`detail/resolver_thread_pool`) | one short-lived detached thread per query |
| Cancellation | a full cancellation queue, per query | a sampled flag, latest query only |
| Results | `results` (iterable, ordered with service hints) | `std::vector<endpoint>` (IPv4) |
| Thread lifetime | pool lives with the context | detached, kept alive by a work unit + user guarantee |
| endpoint type | `udp::endpoint` / `tcp::endpoint`, separate | one `endpoint` reused |
| datagram socket | `basic_datagram_socket.hpp` | `src/ip_udp.cpp` |

The `udp::endpoint` reuse deserves its own note: asio makes them two distinct
types (even though both are an IPv4 address/port pair), catching "send a TCP
endpoint to a UDP socket" at compile time. sasio merges them — fewer types,
but the check defers to runtime. Another case of "the teaching build cuts
type gymnastics": what was cut has a name, registered in the differences
table.

"Thread per query" is the same kind of trade: under heavy queries, thread
creation overhead and uncancellable `getaddrinfo` calls are real costs, and
asio's resident pool plus cancellation queue is the production answer. sasio
gives the **minimum legible** version — thread, work, and post each appear
exactly once, visible at a glance.

## Exercises

**Experiment**: write a UDP echo server, under 30 lines:

```cpp
sasio::io_context ctx;
sasio::ip::udp::socket sock(ctx);
sock.open();
sock.bind(sasio::ip::udp::endpoint(5555));        // same port as ex03

char recv_data[1500];                              // one buffer for receiving
sasio::ip::udp::endpoint sender;

std::function<void()> do_receive = [&]() {
  sock.async_receive_from(sasio::buffer(recv_data), sender,
      [&](sasio::error_code ec, std::size_t n) {
        if (!ec)
        {
          auto payload = std::make_shared<std::vector<char>>(
              recv_data, recv_data + n);           // each send gets its own copy
          sock.async_send_to(sasio::buffer(*payload), sender,
              [payload](sasio::error_code, std::size_t) {});
        }
        do_receive();                              // keep receiving
      });
};
do_receive();
ctx.run();
```

Verify the echo with `nc -u 127.0.0.1 5555`. Note the `payload`: one buffer
for both receiving and sending is not enough — while `async_send_to` is still
outstanding, the next `async_receive_from` has already re-registered on the
same buffer, and a second datagram overwrites the bytes in flight. Each send
copies its own payload, held alive by the lambda's `shared_ptr` until the
send completes (the handler's destruction releases it). Does that hand look
familiar? `write_op`'s relay baton from chapter 8, pocket edition.

**Bug hunt**: two timings — what is `aborted` in each?

- Timing A: `async_resolve` starts → the thread is inside `getaddrinfo` →
  `cancel()` → the lookup finishes → sampling;
- Timing B: `async_resolve` starts → the lookup finishes → **after
  sampling** → `cancel()` → the handler executes.

Confirm line by line against `start_resolve`: A delivers
`operation_aborted`, B delivers the resolution. Then one step further: if the
sampling happened inside the handler (after the post) instead of in the
thread, how long would timing A's cancellation be delayed? — "the sampling
position is the semantics" only counts when it grows out of the code.

---

TCP, UDP, name resolution, composed operations — the API surface of a usable
network library is complete. But under multi-threaded `run()`, two handlers
touching the same `session` at once is sooner or later inevitable. Next
chapter's strand: one queue, one running flag, turning "no concurrency" into
a composable guarantee.
