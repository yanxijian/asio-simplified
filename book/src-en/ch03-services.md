<!-- translated-from: book/src/ch03-services.md sha256:966ab72561e0 -->

# Why `io_context` Is So Thin: The Service Registry

Chapter 2 ended with an open question: when `async_read_some` initiates, it
stores the buffer and handler in the socket and registers interest with the
reactor. But where does the socket get the reactor? Who creates the reactor,
the timer heap, and the handler queue — and when are they destroyed?

The easy answer is to put them all in `io_context`:

```cpp
class io_context {
  Scheduler scheduler_;    // handler queue
  Reactor   reactor_;      // descriptor table
  TimerHeap timers_;       // timers
  // ...one more line here for every new feature
};
```

It works. But look at asio's `io_context` — across its hundred-plus lines
there is hardly one line belonging to a concrete feature, and you will not
find a "reactor member." That is not laziness; asio hands the problem to
another layer: the **service registry**. This layer is why `io_context` can
be so thin.

## Design: A Container That Assembles Itself

sasio's answer lives in two types. `execution_context` is the container,
`service` is the base class of its parts:

```cpp
class service {
public:
  virtual ~service() = default;
  virtual void shutdown() {}   // called at context destruction, reverse order
};

class execution_context {
public:
  template <typename T, typename... Args>
  T& use_service(Args&&... args);          // return if present, create if not

protected:
  void shutdown_services();                // reverse-order shutdown, then destroy
};
```

Two invariants, each guarding one thing:

1. **At most one instance of each service type per context.** The first
   `use_service<reactor>()` creates it; every later call returns the same
   instance. Whoever comes first triggers the creation — a socket
   constructor, the scheduler constructor, user code, all fine;
2. **Shutdown runs in reverse order of creation**. Later services shut down
   first. The scheduler created the reactor, so at teardown the reactor
   shuts down before the scheduler — dependents leave first, dependencies
   clean up after them.

`io_context` inherits `execution_context` and does exactly one thing: ask the
container for a scheduler at construction. The queue, the reactor, and the
timer heap are services that appear on demand; `io_context` knows nothing
about them. Adding a feature (strand in chapter 10, the resolver in
chapter 9) never touches a line of `io_context`.

## The Recursive Construction Chain

"On-demand creation" gets interesting at `io_context`'s constructor:

```cpp
io_context::io_context()
  : scheduler_(use_service<scheduler>())   // first service in the container
{
}
```

That one line triggers more than one creation. The scheduler owns the reactor
and the timer heap, and its constructor asks the container for both:

```cpp
scheduler::scheduler(execution_context& ctx)
  : reactor_(ctx.use_service<reactor>()),   // recursion: create the reactor
    timers_(ctx.use_service<timer_queue>()) // recursion: create the timer heap
{
}
```

So the first `io_context` construction runs: scheduler registers → scheduler
constructs → reactor registers → reactor constructs (initializes Winsock,
opens the interrupter) → timer_queue registers → constructs → back to
scheduler → back to io_context. A recursion chain whose assembly order is
exactly the dependency order — no global "initialization list" arrangement,
everyone triggers what they need.

The same chain explains why invariant 2 matters. Creation order is scheduler
→ reactor → timer_queue, so the shutdown order is timer_queue → reactor →
scheduler: the reactor shuts down first (closing the interrupter and
Winsock), then the scheduler shuts down (draining the queue, stopping
dispatch). Reversed, the `reactor_.interrupt()` inside scheduler's shutdown
would hit an interrupter that is already gone.

## Close Reading: use_service in Twenty Lines

The whole layer's core is one function. From
`include/sasio/execution_context.hpp` (comments are the originals):

```cpp
template <typename T, typename... Args>
T& use_service(Args&&... args)
{
  static_assert(std::is_base_of<service, T>::value,
      "a sasio service must derive from sasio::service");

  std::lock_guard<std::recursive_mutex> lock(mutex_);   // ①

  const std::type_index key(typeid(T));                 // ②
  const auto existing = services_.find(key);
  if (existing != services_.end())
    return static_cast<T&>(*existing->second);          // ③

  std::unique_ptr<T> owned(new T(*this, std::forward<Args>(args)...));
  T& created = *owned;                                  // ④
  services_.emplace(key, std::move(owned));
  order_.push_back(&created);
  return created;
}
```

Four places, each with a reason:

- **① The recursive mutex**. A plain `mutex` deadlocks at ④ —
  `new T(*this, ...)` invokes T's constructor, and the scheduler's
  constructor calls `use_service<reactor>()`, re-entering the same function
  and taking the same lock again. `recursive_mutex` lets one thread re-enter,
  which is what the recursive assembly chain requires. It is the direct
  price of "assemble on demand";
- **② `type_index` as key**. `typeid(T)` wrapped in `std::type_index` drops
  into a hash table — "the scheduler service" and "the reactor service" are
  naturally different keys. asio uses typeid too, just wrapped in its own
  registry structure;
- **③ Hit and return**. This is where invariant 1's "at most one" is honored —
  the second request receives a reference to the same object;
- **④ The context goes into the constructor**. Every service's first
  constructor argument is `*this`, so services can use the container in
  reverse (the scheduler gets its reactor exactly this way). After creation
  the pointer lands in `order_` — the record reverse-order shutdown walks.

One more thing worth a pause: `services_` stores `unique_ptr<service>` base
pointers, and `use_service` downcasts before returning. Polymorphism pushes
all knowledge of "which services exist" out to the call sites; the container
stays ignorant — that is the mechanism behind `io_context`'s thinness.

## Comparison Table

| Question | asio | sasio |
| --- | --- | --- |
| Container | `execution_context.hpp` | `include/sasio/execution_context.hpp` |
| Registry | `detail/service_registry.hpp` | the `use_service` template itself |
| Service base | `execution_context::service` | `sasio::service` |
| Creation | `make_service` + typeid key | `use_service` + typeid key |
| Recursive construction | supported (service_registry re-enters) | supported (`recursive_mutex`) |
| Teardown | reverse-order shutdown | reverse-order via `order_` |
| asio only | `add_service` / `has_service`, `notify_fork`, separate `shutdown`/`destroy` | none (no corresponding need) |

asio's service_registry carries a few facilities sasio lacks: the registry
strings services together on a `first_service_` linked list, supports
post-hoc `add_service` and queries via `has_service`, supports `notify_fork`
(rebuilding internal state after a process fork), and separates
"shutdown (release resources)" from "destroy (free objects)." What does not
change: one instance per service type per context — `basic_io_object.hpp`
constructs I/O objects with `use_service` returning that shared service, and
each object only owns its own `implementation_`. The skeleton — "typeid key +
on-demand creation + reverse shutdown" — is identical, and reading asio's
`service_registry`, you will recognize every line.

## Exercises

**Experiment**: use the registry to write a service of your own — count how
many handlers this context has executed. The whole thing is under 30 lines:

```cpp
class handler_stats : public sasio::service {
public:
  explicit handler_stats(sasio::execution_context& ctx) : service(ctx) {}
  void shutdown() override { std::printf("handlers: %zu\n", count); }

  void on_ran() { ++count; }

private:
  std::size_t count = 0;
};

// usage: wrap each post
auto& stats = ctx.use_service<handler_stats>();
ctx.post([&stats] { stats.on_ran(); /* your real work */ });
ctx.run();   // prints handlers: 1
```

Run it and confirm the printed count matches the number of posts. Then
answer: if two `io_context`s each carry a `handler_stats`, do their counts
interfere? (Hint: reread invariant 1's "per **context**".)

**Bug hunt**: replace the `recursive_mutex` in `execution_context.hpp` with a
plain `std::mutex`, rebuild, and run ch1's `ex01_post`. Predict what happens
and where, then run and compare. If you predicted deadlock — on which entry
does it stick? In the stack trace, whose calls are the two `use_service`
frames?

---

The container is in place and its first service, the scheduler, already
sits inside. But so far we have only said "handlers in the queue get run" —
the full run loop, the work-accounting rules, and the exact definition of
"no work left" are still unopened. Next chapter dissects the scheduler: the
most important 83 lines in the library.
