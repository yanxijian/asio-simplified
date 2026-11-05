<!-- translated-from: book/src/ch08-composed.md sha256:33d0970b98ee -->

# Composed Operations: Operations on Operations

Chapter 7's echo server contained a less flattering stretch —
`session::do_write_some`:

```cpp
void do_write(std::size_t length)
{
  write_size_ = length;
  write_offset_ = 0;
  do_write_some();
}

void do_write_some()
{
  auto self = shared_from_this();
  socket_.async_write_some(
      sasio::buffer(data_ + write_offset_, write_size_ - write_offset_),
      [this, self](error_code ec, std::size_t sent) {
        if (ec)
          return;
        write_offset_ += sent;
        if (write_offset_ < write_size_)
          do_write_some();       // not finished, continue
        else
          do_read();
      });
}
```

This is the real price of "write the whole buffer": manual offsets, manual
re-initiation, manual error handling — and the logic must be **copied at
every call site** that needs a complete write. Writing is bad enough; reading
is worse (TCP is a byte stream, and one `recv` returns however much the
kernel feels like — "read exactly N bytes" is doomed to loop). This chapter
lifts those loops into a reusable operation: `async_write(stream, buffer,
handler)` and `async_read(stream, buffer, handler)` — initiate once, and the
completion guarantees "the whole buffer is handled" (or carries a clear
error). They perform no new system calls; they simply **chain multiple
`async_write_some` / `async_read_some` calls together**, hence composed
operations.

## Design: A State Machine Caught by a Relay Baton

The composed operation's state is startlingly small:

```cpp
template <typename Stream>
class write_op : public std::enable_shared_from_this<write_op<Stream>>
{
  Stream& stream_;            // target (a reference, not owned)
  const_buffer buffer_;       // the whole buffer to write
  std::function<void(error_code, std::size_t)> handler_;  // final completion
  std::size_t transferred_ = 0;   // bytes written so far
};
```

Two invariants:

1. **The state object's lifetime equals the operation chain's**. Every
   intermediate completion handler captures `shared_from_this()`; the chain
   carries the state wherever it goes. When the chain ends (the final
   completion runs), the last capture releases and the state destroys
   itself. No leak, no dangling, no manual delete;
2. **Each completion only advances, then initiates the next segment**.
   `transferred_ += bytes`, and the next `async_write_some` transmits only
   the remaining range (`buffer_.data() + transferred_`, length
   `buffer_.size() - transferred_`). Termination has four doors: full
   (success), error, zero-byte return (a defensive stop against spinning).

That shape — **each completion creates the next initiation** — is the entire
mechanism of composition. It touches neither the reactor nor the queue; it
glues simple operations into complex ones with a callback chain. asio's
`async_read` / `async_write` work the same way, with the gluing made
pluggable by completion conditions (see the table).

## The Life of a 100-Byte Write

Suppose the send buffer swallows only 60 bytes at a time:

```text
async_write(s, 100B, done)
 └─ write_op constructed (transferred_=0)
     └─ s.async_write_some(buf[0..100))
         └─ poll reports writable → do_write → send writes 60
             └─ post(w1: transferred_=60, 60<100 → continue)
                 └─ s.async_write_some(buf[60..100))
                     └─ poll reports writable → do_write → send writes 40
                         └─ post(w2: transferred_=100 → complete(empty error))
                             └─ done(empty error, 100)
```

Two **anonymous intermediate handlers** (w1, w2) appear — lambdas capturing
`self` (the write_op). The user initiated one `async_write`; the engine ran
two real `send`s and posted two intermediate completions; the **final handler
is invoked by `complete()` directly inside w2's call stack** — no third
enqueue, but still inside the run loop, so contract two holds. Contract one
is honored across the chain: every intermediate handler arrived via `post`;
nothing executed inside an initiating function.

The chain's end deserves note: when w2 runs, `transferred_ == buffer_.size()`,
and `do_write` decides completion at its opening — **the last pass initiates
nothing**, it calls the final handler. The intermediate handlers release
`self` one by one; when w2 finishes and releases the last reference, the
write_op and its state destroy together. Invariant 1's "lives to the end of
the chain" is honored automatically.

## Close Reading: write.hpp's Relay Core

From `include/sasio/write.hpp` (construction and members elided):

```cpp
void start() { do_write(); }

private:
void do_write()
{
  if (transferred_ == buffer_.size())       // full: done successfully
  {
    complete(error_code());
    return;
  }

  auto self = this->shared_from_this();     // ← the relay baton
  const char* data = static_cast<const char*>(buffer_.data()) + transferred_;

  stream_.async_write_some(
      const_buffer(data, buffer_.size() - transferred_),   // only the rest
      [self](error_code ec, std::size_t bytes) {
        self->transferred_ += bytes;

        if (ec)
        {
          self->complete(ec);               // error: finish with bytes written
          return;
        }

        // A zero-byte write would spin; treat it as completion.
        if (bytes == 0)
        {
          self->complete(error_code());     // zero bytes: defensive stop
          return;
        }

        self->do_write();                   // advance and re-initiate
      });
}

void complete(error_code ec)
{
  std::function<void(error_code, std::size_t)> handler = std::move(handler_);
  handler_ = nullptr;
  if (handler)
    handler(ec, transferred_);
}
```

Four details, each easy to overlook:

- **`auto self = shared_from_this()` captures before initiating**. The
  lambda holds one reference for the whole window from "initiate the next
  segment" to "that segment's completion runs" — the state lives through it
  all. Miss this line and the write_op may be destroyed by the time the
  intermediate completion runs — the classic use-after-free of callback
  chains;
- **`complete` moves the handler out before nulling it**. The final handler
  runs exactly once — even if a future edit created two completion paths,
  the second stops at `if (handler)`. Once-only semantics come from "take
  away," not "remember";
- **A zero-byte write is a termination condition, not a loop condition**.
  `async_write_some` returning 0 makes no progress; re-initiating would
  return 0 forever — treat it as completion (with the bytes written), the
  same as asio;
- **On error the completion reports `transferred_`, not 0**. "The peer
  disconnected after 60 bytes" completes as `(connection_reset, 60)` — the
  caller needs to know the part that happened. `async_read` does the same
  for EOF, with `sasio::eof` as the error.

`async_write` itself is a thin shell, and one branch deserves its own look —
**the empty buffer**:

```cpp
if (buffer.size() == 0)
{
  stream.context().post([erased]() mutable {
    erased(error_code(), 0);
  });
  return;
}
```

Nothing to do, and still through `post` — not a direct
`handler(error_code(), 0)`. Contract two again: even a degenerate chain with
zero work must complete inside `run()`. The branch guarantees
`async_write(s, buffer(s, 0), h)` is semantically identical to any other
`async_write`.

`read.hpp` mirrors write with two differences: the finish line is "buffer
full" instead of "buffer drained," and **peer closure (`async_read_some`
returning 0 bytes) is promoted to the `eof` error** — the buffer can never
fill, so the operation completes with the bytes read so far and
`sasio::eof`. Note the contrast with `async_read_some`: for the some-form, a
0-byte return is the normal report of closure; for the composed form it
becomes an error, because the "read it full" promise can no longer be kept.

## Comparison Table

| Question | asio | sasio |
| --- | --- | --- |
| State machine | `detail::write_op` / `read_op` in `impl/write.hpp` and `impl/read.hpp` | the same-named classes in `include/sasio/write.hpp` / `read.hpp` |
| Completion condition | pluggable: `transfer_all()` (default), `transfer_exactly(n)`, `transfer_at_least(n)`, hosted in `detail/base_from_completion_cond.hpp` | hardcoded "everything" |
| Buffer sequences | `ConstBufferSequence` (multi-buffer, scatter/gather) | a single `const_buffer` / `mutable_buffer` |
| EOF semantics | `async_read` completes with `error::eof` | same (`sasio::eof`) |
| Stream variants | `async_read_until` (dynamic buffer + delimiter) | none (registered in the differences table) |

asio's completion conditions deserve a read: `base_from_completion_cond`
makes "how much is enough" a policy base class, and `write_op` asks it at
every completion. sasio hardcodes `transfer_all` — the trade-off of cutting
generalization while keeping the trunk, and the exercise adds it back.

## Exercises

**Experiment**: upgrade the echo server to its second form — delete the
hand-written `do_write_some` loop entirely, and call from `do_read`'s
completion:

```cpp
sasio::async_write(socket_, sasio::buffer(data_, length),
    [this, self](error_code ec, std::size_t)
    {
      if (ec)
        return;
      do_read();
    });
```

Behavior is identical to the hand-written loop, but `write_offset_`,
`write_size_`, and the whole `do_write_some` are gone — the state moved into
`write_op`, its lifetime managed by the shared_ptr relay. Compare the two
versions and answer: where was the hand-written `write_offset_`? Where is the
composed version's? Why does the session not need to hold it?

**Bug hunt + extension**: add `transfer_at_least(n)` to sasio — complete
after **at least** n bytes (asio's same-named completion condition). Hint:
add a `std::size_t least_` member and constructor parameter to `read_op` /
`write_op`, and change the two completion tests from
`transferred_ == buffer_.size()` to
`transferred_ >= least_` (or a combined form); add an overload of
`async_read` / `async_write` taking `least`. When done, ask: does
`transfer_at_least(0)` exactly equal the default behavior? For the
combination of a 0-byte `async_read_some` return with `eof`, should
`least_ = 0` be success or error? To align with asio, find its decision
order in `base_from_completion_cond.hpp` — and note the actual order in
`impl/read.hpp`: the completion condition is checked when `read_op` is
invoked, before any I/O.

---

One read, one write, plus UDP and name resolution, and the API surface of a
usable network library is complete. The next chapter fits in the last two
pieces: UDP's `send_to` / `receive_from` (the reactor's second use), and the
resolver that throws "turn this name into an address" at a worker thread
(the only component with a thread).
