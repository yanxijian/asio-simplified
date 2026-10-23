//
// scheduler.hpp
// ~~~~~~~~~~~~~
//
// The scheduler is where the "proactor" part of Asio becomes visible:
//
//   1. A handler is always queued, never called directly by the initiating
//      function (except for the documented inline dispatch case).
//   2. Every queued handler carries one unit of work, which is released after
//      the handler returns. In asio this unit lives in the operation object
//      (scheduler_operation + work_cleanup); here it is a counter plus the
//      queue, but the rule is the same.
//   3. run() alternates between "run one queued handler" and "block until the
//      reactor says something happened, or a timer expires".
//   4. When the work count reaches zero the scheduler stops itself, so a
//      second run() needs restart() first - exactly like asio.
//
// asio counterpart: asio/detail/scheduler.hpp and scheduler.ipp.
//
#ifndef SASIO_SCHEDULER_HPP
#define SASIO_SCHEDULER_HPP

#include "sasio/execution_context.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>

namespace sasio {

class reactor;
class timer_queue;

class scheduler : public service
{
public:
  explicit scheduler(execution_context& ctx);
  ~scheduler() override = default;

  void shutdown() override;

  void post(std::function<void()> handler);
  void dispatch(std::function<void()> handler);

  std::size_t run();
  std::size_t run_one();
  std::size_t poll();
  std::size_t poll_one();

  void stop();
  bool stopped() const;
  void restart();

  void on_work_started();
  void on_work_finished();

  // Wake a thread that is blocked in reactor::run_for(), so it re-reads the
  // queue, the timer heap and the descriptor table. Used when a timer becomes
  // the new earliest one from outside the run() loop.
  void wake_waiters();

private:
  using clock = std::chrono::steady_clock;

  bool run_one_impl(bool block);
  std::size_t run_impl(bool block);
  void invoke(std::function<void()> handler);

  // Lock order is scheduler mutex -> timer queue mutex -> reactor mutex.
  // Nothing takes them in the opposite order, so the three cannot deadlock.
  bool has_work_locked() const;
  bool has_work() const;

  // Stops the scheduler if there is nothing left that could produce a handler.
  // asio reaches the same state from work_finished().
  void maybe_stop_if_idle();

  reactor& reactor_;
  timer_queue& timers_;

  mutable std::mutex mutex_;
  std::deque<std::function<void()>> queue_;
  std::size_t work_count_ = 0;
  std::atomic<bool> stopped_{false};
};

} // namespace sasio

#endif // SASIO_SCHEDULER_HPP
