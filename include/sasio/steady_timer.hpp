//
// steady_timer.hpp
// ~~~~~~~~~~~~~~~~
//
// A timer bound to std::chrono::steady_clock. The public shape follows
// asio::steady_timer (which is basic_waitable_timer<steady_clock>):
//
//   timer.expires_after(200ms);
//   timer.async_wait([](error_code ec) { ... });
//
// Deliberate simplification: asio allows any number of simultaneous waits on
// one timer object, and expires_at() cancels all of them. Here a timer holds
// at most one pending wait; a new async_wait() cancels the previous one.
//
// asio counterpart: asio/steady_timer.hpp, asio/basic_waitable_timer.hpp
//
#ifndef SASIO_STEADY_TIMER_HPP
#define SASIO_STEADY_TIMER_HPP

#include "sasio/error.hpp"
#include "sasio/timer_queue.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

namespace sasio {

class io_context;
class scheduler;

class steady_timer
{
public:
  using clock = std::chrono::steady_clock;
  using time_point = clock::time_point;
  using duration = clock::duration;

  explicit steady_timer(io_context& ctx);
  ~steady_timer();

  steady_timer(const steady_timer&) = delete;
  steady_timer& operator=(const steady_timer&) = delete;

  time_point expiry() const { return expiry_; }

  // Setting a new expiry cancels any wait that is already pending, just like
  // asio's expires_at().
  std::size_t expires_at(time_point t);
  std::size_t expires_after(duration d) { return expires_at(clock::now() + d); }

  // Queue one wait. The handler is invoked exactly once, with an empty
  // error_code on expiry or operation_aborted if the wait was cancelled.
  void async_wait(std::function<void(error_code)> handler);

  // Cancel the pending wait, if any. The cancelled handler is not called from
  // inside cancel(): it is posted to the io_context, exactly like asio, so a
  // cancellation can never re-enter the caller.
  std::size_t cancel();

private:
  timer_queue& timers_;
  scheduler& scheduler_;
  time_point expiry_ = time_point();
  std::shared_ptr<timer_queue::entry> pending_;
};

} // namespace sasio

#endif // SASIO_STEADY_TIMER_HPP
