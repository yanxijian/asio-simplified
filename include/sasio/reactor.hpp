//
// reactor.hpp
// ~~~~~~~~~~~
//
// On POSIX, Asio turns readiness notifications into "completion" events: the
// reactor watches descriptors, and when one is ready it runs the handler that
// was waiting on it. That is the classic reactor pattern, dressed up to look
// like the Windows proactor (IOCP) API.
//
// this simplified version uses WSAPoll, which is available on every supported
// Windows version and - unlike select() - has no FD_SETSIZE limit. In asio the
// same job is done by detail/epoll_reactor (Linux) or detail/select_reactor
// (portable fallback); the structure is the same:
//
//   * a list of registered descriptors and the operation waiting on each,
//   * a wait (poll) that reports readiness,
//   * an "interrupter" so another thread can wake the wait up when a handler
//     is posted or the context is stopped.
//
// Chapter 1 only needs the interrupter: timers do not use descriptors. Chapter
// 2 adds the registration table for sockets.
//
// asio counterpart: asio/detail/select_reactor.hpp, asio/detail/epoll_reactor.hpp
//
#ifndef SASIO_REACTOR_HPP
#define SASIO_REACTOR_HPP

#include "sasio/config.hpp"
#include "sasio/execution_context.hpp"

#include <atomic>
#include <chrono>

namespace sasio {

class reactor : public service
{
public:
  using clock = std::chrono::steady_clock;

  explicit reactor(execution_context& ctx);
  ~reactor() override;

  void shutdown() override;

  // Wait for readiness or an interrupt, for at most `timeout`. A timeout of
  // clock::duration::max() means "wait indefinitely".
  void run_for(clock::duration timeout);

  // Wake up a thread blocked in run_for(). Called by the scheduler after a
  // handler is queued, and by stop().
  void interrupt();

  // True when no descriptor is registered and therefore nothing but timers
  // and posted handlers can make progress.
  bool idle() const { return true; }

private:
  void open_interrupter();
  void close_interrupter();

  SOCKET interrupter_ = INVALID_SOCKET;
  std::atomic<bool> interrupted_{false};
  bool winsock_initialised_ = false;
};

} // namespace sasio

#endif // SASIO_REACTOR_HPP
