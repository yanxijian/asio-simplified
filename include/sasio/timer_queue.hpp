//
// timer_queue.hpp
// ~~~~~~~~~~~~~~~
//
// A min-heap of pending timer waits, ordered by expiry time. The scheduler
// asks two questions:
//
//   * when does the next timer expire? (used as the poll timeout)
//   * has anything expired by now?     (asked after every wait)
//
// Keeping this separate from the reactor mirrors asio: timers are serviced by
// the scheduler, not by the descriptor wait. asio has one timer queue per
// timer type (steady, system, high resolution, ...); we only need steady.
//
// asio counterpart: asio/detail/timer_queue.hpp
//
#ifndef SASIO_TIMER_QUEUE_HPP
#define SASIO_TIMER_QUEUE_HPP

#include "sasio/error.hpp"
#include "sasio/execution_context.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace sasio {

class timer_queue : public service
{
public:
  using clock = std::chrono::steady_clock;

  struct entry
  {
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    clock::time_point when;
    std::function<void(error_code)> handler;
    std::size_t heap_index = npos;
  };

  explicit timer_queue(execution_context& ctx);
  ~timer_queue() override = default;

  void shutdown() override;

  // Adds the entry and reports whether it became the earliest one. The
  // caller uses that to wake a thread that is already blocked waiting: its
  // poll timeout was computed before this timer existed. asio does the same
  // in select_reactor::schedule_timer():
  //   bool earliest = queue.enqueue_timer(time, timer, op);
  //   if (earliest) interrupter_.interrupt();
  bool add(std::shared_ptr<entry> e);

  // Returns false when the entry had already expired or was cancelled.
  bool remove(const std::shared_ptr<entry>& e);

  bool empty() const;
  clock::time_point next_expiry() const;

  // Removes and returns the earliest entry if it is due, otherwise null.
  std::shared_ptr<entry> take_expired(clock::time_point now);

private:
  void swap_at(std::size_t a, std::size_t b);
  void sift_up(std::size_t index);
  void sift_down(std::size_t index);
  void pop_at(std::size_t index);

  mutable std::mutex mutex_;
  std::vector<std::shared_ptr<entry>> heap_;
};

} // namespace sasio

#endif // SASIO_TIMER_QUEUE_HPP
