//
// reactor.hpp
// ~~~~~~~~~~~
//
// On POSIX, Asio turns readiness notifications into "completion" events: the
// reactor watches descriptors, and when one is ready it runs the handler that
// was waiting on it. That is the classic reactor pattern, dressed up to look
// like the Windows proactor (IOCP) API.
//
// This simplified version uses WSAPoll, which is available on every supported
// Windows version and - unlike select() - has no FD_SETSIZE limit. In asio the
// same job is done by detail/epoll_reactor (Linux) or detail/select_reactor
// (the portable fallback); the structure is the same:
//
//   * a table of registered descriptors and the handler waiting on each,
//   * a wait (poll) that reports readiness,
//   * an "interrupter" so another thread can wake the wait up when a handler
//     is posted or the context is stopped.
//
// Chapter 1 needed only the interrupter (timers do not use descriptors).
// Chapter 2 adds the registration table used by sockets and acceptors.
//
// asio counterpart: asio/detail/select_reactor.hpp, asio/detail/epoll_reactor.hpp
//
#ifndef SASIO_REACTOR_HPP
#define SASIO_REACTOR_HPP

#include "sasio/config.hpp"
#include "sasio/execution_context.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

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

  // True when no descriptor is registered. Until then the reactor itself is
  // what keeps run() alive, the same way a pending timer does.
  bool idle() const;

  // Register interest in read/write readiness. Only one handler per direction
  // is supported, which matches the "one operation per direction" rule that
  // the socket class relies on.
  void start_read(SOCKET descriptor, std::function<void()> handler);
  void stop_read(SOCKET descriptor);
  void start_write(SOCKET descriptor, std::function<void()> handler);
  void stop_write(SOCKET descriptor);
  void close_descriptor(SOCKET descriptor);

private:
  struct descriptor_state
  {
    std::function<void()> read_handler;
    std::function<void()> write_handler;

    bool empty() const { return !read_handler && !write_handler; }
  };

  void open_interrupter();
  void close_interrupter();
  void remove_if_empty(SOCKET descriptor);

  SOCKET interrupter_ = INVALID_SOCKET;
  std::atomic<bool> interrupted_{false};
  bool winsock_initialised_ = false;

  mutable std::mutex mutex_;
  std::unordered_map<SOCKET, descriptor_state> descriptors_;
};

} // namespace sasio

#endif // SASIO_REACTOR_HPP
