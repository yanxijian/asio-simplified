//
// io_context.hpp
// ~~~~~~~~~~~~~~
//
// The front door of the library. In asio, io_context is an execution_context
// that adds only a handful of things on top:
//
//   * a scheduler (detail::scheduler) holding the handler queue,
//   * a reactor (detail::select_reactor / epoll_reactor) waiting for readiness,
//   * a timer queue (detail::timer_queue),
//   * run()/run_one()/poll()/poll_one() driving all of the above.
//
// We keep exactly that shape.
//
// asio counterpart: asio/io_context.hpp
//
#ifndef SASIO_IO_CONTEXT_HPP
#define SASIO_IO_CONTEXT_HPP

#include "sasio/execution_context.hpp"

#include <cstddef>
#include <functional>

namespace sasio {

class scheduler;

class io_context : public execution_context
{
public:
  class work;

  io_context();
  ~io_context() override;

  io_context(const io_context&) = delete;
  io_context& operator=(const io_context&) = delete;

  // Queue a handler to run later, on some thread that calls run()/poll().
  // Any thread may call post().
  void post(std::function<void()> handler);

  // Run the handler now if we are already inside this context on this thread,
  // otherwise behave like post(). asio decides this through the handler's
  // associated executor; a thread-local marker is enough here.
  void dispatch(std::function<void()> handler);

  // Run handlers until there is no more work. Running out of work stops the
  // context (stopped() becomes true), so a second round needs restart() first
  // - this is asio's behaviour, not an accident of this implementation.
  // One handler may be running on several threads at the same time if several
  // threads call run(); strand exists in asio to prevent that.
  std::size_t run();
  std::size_t run_one();

  // Do not block: run whatever is ready right now and return.
  std::size_t poll();
  std::size_t poll_one();

  // Cause run() to return as soon as the current handler finishes.
  void stop();
  bool stopped() const;

  // Undo stop(), so run() can be called again.
  void restart();

private:
  scheduler& scheduler_;
};

// Keeps run() alive even when no asynchronous operation is outstanding.
// This is asio::executor_work_guard<io_context::executor_type>.
class io_context::work
{
public:
  explicit work(io_context& ctx);

  work(const work&) = delete;
  work& operator=(const work&) = delete;

  work(work&& other) noexcept;
  work& operator=(work&& other) noexcept;

  ~work();

  // Same as asio::executor_work_guard::reset(): release the work unit early.
  void reset();

private:
  scheduler* scheduler_;
};

} // namespace sasio

#endif // SASIO_IO_CONTEXT_HPP
