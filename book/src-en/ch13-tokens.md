<!-- translated-from: book/src/ch13-tokens.md sha256:1c2087b7f933 -->

# The Completion-Token Layer: How Results Come Back

The executor layer answered "where to run"; this chapter — the layer's other
half — answers **in what form the result returns to the caller**. The same
initiating function,

```cpp
socket.async_read_some(buffer, /* what goes here? */);
```

has callers wanting entirely different things: some want a callback, some a
`std::future`, some a coroutine, some do not care about the result at all
(fire and forget). asio's answer is to make "how the result comes back" a
compile-time parameter — the **completion token** — with a specialization
mechanism so the same initiating function has exactly one implementation per
result form.

## Design: The Token Is a Compile-Time Policy

Compress the mechanism into three rules:

1. **Every initiating function declares its completion signature**.
   `async_read_some`'s signature is `void(error_code, std::size_t)`; the
   initiating function does not handle the token itself — it hands the
   signature and the token together to `async_result`;
2. **`async_result<Token, Signature>`'s specializations decide two things**:
   the initiating function's return type, and what "the completion handler
   actually given to the engine" takes shape. The default specialization
   (the token is a plain callback) returns `void` and the handler is the
   token itself — the twenty years of callback code you have written all
   walked through this default specialization;
3. **Users can define their own tokens**. Specialize `async_result` for a new
   type and it becomes a first-class citizen of the library — `use_future`,
   `use_awaitable`, `deferred`, `detached` are all users of this mechanism,
   not engine hardcoding.

sasio lacks the system: the callback is the only form, and coroutines rely on
the explicit adapters in `co_spawn.hpp`. This chapter opens asio's
specialization system and answers "if we installed it on sasio, what is the
minimum."

## Trace: Four Tokens, Four Paths

The same initiating call, four tokens, four paths:

```text
callback:   async_read_some(buf, my_lambda)
              → the async_result default specialization
              → completion_handler = my_lambda
              → initiate → complete → my_lambda(ec, n) runs in run()

detached:   async_read_some(buf, asio::detached)
              → the detached specialization → the completion handler is
                a no-op object → complete → nothing happens
              (errors swallowed too — use with care)

use_future: async_read_some(buf, asio::use_future)
              → the use_future specialization → the return value becomes
                std::future<size_t>
              → the completion handler assigns the promise → you .get() elsewhere

use_awaitable: co_spawn(ctx, session(), asio::detached)
              → every co_await inside the coroutine resumes through the
                awaitable frame
              → the two ends of the same road as sasio's async_operation
```

Chapter 2's trace asked "what does the initiating function return, and who is
the handler" — now it can be exact: **both are decided by the `async_result`
specialization**. The initiating function's shape is reduced to a fixed
skeleton (`async_initiate`), and only the token varies.

## Close Reading: async_result's Primary Template

`asio/async_result.hpp`'s primary template (documentation comments stripped):

```cpp
template <typename CompletionToken,
    ASIO_COMPLETION_SIGNATURE... Signatures>
class async_result :
  public conditional_t<
      detail::are_any_lvalue_completion_signatures<Signatures...>::value
        || !detail::are_any_rvalue_completion_signatures<Signatures...>::value,
      detail::completion_handler_async_result<CompletionToken, Signatures...>,
      async_result<CompletionToken,
        typename detail::simple_completion_signature<Signatures>::type...>
    >
{
public:
  typedef CompletionToken completion_handler_type;

  typedef void return_type;

  explicit async_result(completion_handler_type& h);

  return_type get();

  template <typename Initiation, typename RawCompletionToken, typename... Args>
  static return_type initiate(
      Initiation&& initiation,
      RawCompletionToken&& token,
      Args&&... args);
};
```

The primary template describes the **default behavior** (the token is the
callback, void return); each token type provides its own specialization,
overriding `return_type` and `completion_handler_type`. `initiate` is the
crucial design — it splits the initiating function into two halves:

- **Token handling** (the specialization's job): use_future's specialization
  builds a promise, makes "assign the promise" the completion handler, and
  hands `future` back as the return value;
- **Actual initiation** (`Initiation`'s job): really calls the socket's
  `async_read_some` implementation with the processed completion handler,
  registering into the engine.

Split this way, an initiating function's author writes `Initiation` once;
however the token system evolves, the initiating function does not change.
That is the machinery behind "one implementation, four result forms."

## Three Columns: Absent ↔ Implementation ↔ Where to Find It

| Capability | sasio today | asio implementation | where in asio |
| --- | --- | --- | --- |
| Callback (the default form) | yes (the only form) | `async_result`'s default specialization | `asio/async_result.hpp` |
| future | none | the `use_future` token | `asio/use_future.hpp` |
| coroutine access | yes (explicit adapters in `co_spawn.hpp`, no token) | the `use_awaitable` token + `co_spawn` | `asio/use_awaitable.hpp`, `asio/co_spawn.hpp` |
| deferred start | none | `deferred` (the token is itself a callable) | `asio/deferred.hpp` |
| discard the result | none (a handler must exist) | `detached` (a no-op handler) | `asio/detached.hpp` |
| initiating-function skeleton | written directly | `async_initiate` splits token from initiation | the `initiate` in `asio/async_result.hpp` |
| completion-signature checking | none | the `completion_signature` concept | `asio/async_result.hpp` (`is_completion_signature`) |

The scale comparison: sasio's `co_spawn.hpp` (113 lines) serves one form —
"coroutine access"; asio's `use_awaitable.hpp` + `impl/co_spawn.hpp` + the
`async_result` system serves **any** form with the same initiating
implementation. What was added is not "a better coroutine" — it is "the
result form itself became an extension point."

## Exercises

**Experiment**: hand-write a minimal future token for sasio. No generics
required, only semantics: a `read_with_future(socket, buffer)` returning
`std::future<std::size_t>` (errors as exceptions):

```cpp
std::future<std::size_t> read_with_future(
    sasio::ip::tcp::socket& socket, sasio::mutable_buffer buf)
{
  auto promise = std::make_shared<std::promise<std::size_t>>();
  auto future = promise->get_future();
  socket.async_read_some(buf,
      [promise](sasio::error_code ec, std::size_t n) mutable
      {
        if (ec)
          promise->set_exception(
              std::make_exception_ptr(std::system_error(ec)));
        else
          promise->set_value(n);
      });
  return future;
}
```

Run it, then compare with chapter 8: what this ten-line function does is the
same kind of thing as `write_op`'s shared_ptr relay — handing "what to do at
completion," with its state, to the engine. One step further: why does asio
not write a `read_with_xxx` per result form? The answer is in the
`async_initiate` split.

**Bug hunt**: chapter 11's sasio coroutine adapters (the free functions in
`co_spawn.hpp`) are one function per entry. Against asio: how does the same
operation support both a callback and `use_awaitable`? Open
`asio/use_awaitable.hpp`, find the `async_result` specialization's
"token becomes completion handler" part, and answer: when the completion
handler is called, who resumes the coroutine? (Following it you will reach
`awaitable_frame::resume` — the same-named sibling of chapter 11's
`awaiting_.resume()`.)

---

The executor and completion-token layers are both dissected. The book's
opening question can now be answered: of asio's 169,000 lines, what is model
and what is machinery? The next chapter gives the complete reading map —
including mapping sasio's nine chapters onto the asio source tree, and a
six-week reading plan.
