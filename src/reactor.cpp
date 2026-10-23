#include "sasio/reactor.hpp"

#include "sasio/error.hpp"

#include <climits>
#include <system_error>
#include <utility>

namespace sasio {
namespace {

// poll() takes a signed int of milliseconds. clock::duration::max() is our
// spelling of "no timeout".
int to_milliseconds(reactor::clock::duration timeout)
{
  if (timeout == reactor::clock::duration::max())
    return -1;

  const long long millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();

  if (millis <= 0)
    return 0;
  if (millis > INT_MAX)
    return INT_MAX;
  return static_cast<int>(millis);
}

} // namespace

reactor::reactor(execution_context&)
{
  // Winsock must be initialised before any socket call. asio does this in
  // detail/winsock_init.hpp, with a reference count shared by all contexts.
  WSADATA data;
  if (::WSAStartup(MAKEWORD(2, 2), &data) != 0)
    throw std::system_error(last_socket_error(), "WSAStartup failed");
  winsock_initialised_ = true;

  open_interrupter();
}

reactor::~reactor()
{
  shutdown();
}

void reactor::shutdown()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptors_.clear();
  }
  close_interrupter();

  if (winsock_initialised_)
  {
    ::WSACleanup();
    winsock_initialised_ = false;
  }
}

void reactor::open_interrupter()
{
  // A UDP socket connected to itself. Sending one byte to it makes WSAPoll
  // report read readiness, which is how another thread wakes this one up.
  // asio uses an internal pipe (POSIX) or a socket pair (Windows).
  interrupter_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (interrupter_ == INVALID_SOCKET)
    throw std::system_error(last_socket_error(), "socket() failed");

  // If the remaining setup fails, the socket must not leak: the object is not
  // fully constructed, so its destructor will not run.
  auto fail = [this](const char* what) -> void {
    const error_code error = last_socket_error();
    close_interrupter();
    throw std::system_error(error, what);
  };

  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;

  if (::bind(interrupter_, reinterpret_cast<sockaddr*>(&address),
          sizeof(address)) == SOCKET_ERROR)
    fail("bind() failed");

  int length = sizeof(address);
  if (::getsockname(interrupter_, reinterpret_cast<sockaddr*>(&address),
          &length) == SOCKET_ERROR)
    fail("getsockname() failed");

  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (::connect(interrupter_, reinterpret_cast<sockaddr*>(&address),
          sizeof(address)) == SOCKET_ERROR)
    fail("connect() failed");

  // Non-blocking so the drain loop below can never hang.
  u_long non_blocking = 1;
  if (::ioctlsocket(interrupter_, FIONBIO, &non_blocking) == SOCKET_ERROR)
    fail("ioctlsocket() failed");
}

void reactor::close_interrupter()
{
  if (interrupter_ != INVALID_SOCKET)
  {
    ::closesocket(interrupter_);
    interrupter_ = INVALID_SOCKET;
  }
  interrupted_.store(false, std::memory_order_release);
}

void reactor::interrupt()
{
  if (interrupter_ == INVALID_SOCKET)
    return;

  // Collapse any number of interrupts into one byte. The waiting thread only
  // needs to know "something happened"; the handler queue itself is the real
  // source of truth.
  if (interrupted_.exchange(true, std::memory_order_acq_rel))
    return;

  const char byte = 0;
  if (::send(interrupter_, &byte, 1, 0) == SOCKET_ERROR)
  {
    // Nothing was written, so do not leave the "already interrupted" flag set:
    // the next interrupt must try again.
    interrupted_.store(false, std::memory_order_release);
  }
}

bool reactor::idle() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return descriptors_.empty();
}

void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  bool first = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_state& state = descriptors_[descriptor];
    first = !state.read_handler;
    state.read_handler = std::move(handler);
  }

  // A thread already blocked in WSAPoll() is waiting on a snapshot of the
  // descriptor set taken before this registration, so it must be woken up to
  // rebuild that set. asio does exactly this in
  // select_reactor::start_unready_op():
  //   if (first) interrupter_.interrupt();
  if (first)
    interrupt();
}

void reactor::stop_read(SOCKET descriptor)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = descriptors_.find(descriptor);
  if (it == descriptors_.end())
    return;

  it->second.read_handler = nullptr;
  if (it->second.empty())
    descriptors_.erase(it);
}

void reactor::start_write(SOCKET descriptor, std::function<void()> handler)
{
  bool first = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_state& state = descriptors_[descriptor];
    first = !state.write_handler;
    state.write_handler = std::move(handler);
  }

  // See start_read(): the poll set is a snapshot, so new interest must wake a
  // thread that is already waiting in WSAPoll().
  if (first)
    interrupt();
}

void reactor::stop_write(SOCKET descriptor)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = descriptors_.find(descriptor);
  if (it == descriptors_.end())
    return;

  it->second.write_handler = nullptr;
  if (it->second.empty())
    descriptors_.erase(it);
}

void reactor::close_descriptor(SOCKET descriptor)
{
  std::lock_guard<std::mutex> lock(mutex_);
  descriptors_.erase(descriptor);
}

void reactor::run_for(clock::duration timeout)
{
  if (interrupter_ == INVALID_SOCKET)
    return;

  std::vector<WSAPOLLFD> poll_fds;
  std::vector<SOCKET> polled_sockets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    poll_fds.reserve(descriptors_.size() + 1);
    polled_sockets.reserve(descriptors_.size());

    for (const auto& entry : descriptors_)
    {
      const descriptor_state& state = entry.second;
      WSAPOLLFD poll_fd = {};
      poll_fd.fd = entry.first;
      poll_fd.events = 0;
      if (state.read_handler)
        poll_fd.events |= POLLRDNORM;
      if (state.write_handler)
        poll_fd.events |= POLLWRNORM;
      if (poll_fd.events == 0)
        continue;

      poll_fds.push_back(poll_fd);
      polled_sockets.push_back(entry.first);
    }

    WSAPOLLFD interrupt_fd = {};
    interrupt_fd.fd = interrupter_;
    interrupt_fd.events = POLLRDNORM;
    poll_fds.push_back(interrupt_fd);
  }

  const int result =
      ::WSAPoll(poll_fds.data(), static_cast<ULONG>(poll_fds.size()),
          to_milliseconds(timeout));

  if (result == SOCKET_ERROR)
  {
    const error_code error = last_socket_error();
    if (error.value() == WSAEINTR)
      return;
    throw std::system_error(error, "WSAPoll() failed");
  }

  // Collect the handlers whose descriptors are ready, then run them with no
  // lock held. A handler is moved out of the table so that a level-triggered
  // poll cannot deliver the same readiness twice.
  std::vector<std::function<void()>> ready_handlers;
  if (result > 0)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < polled_sockets.size(); ++i)
    {
      const short revents = poll_fds[i].revents;
      if (revents == 0)
        continue;

      const auto it = descriptors_.find(polled_sockets[i]);
      if (it == descriptors_.end())
        continue;

      descriptor_state& state = it->second;
      const bool error_or_hangup =
          (revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;

      if (state.read_handler && (error_or_hangup || (revents & POLLRDNORM)))
      {
        ready_handlers.push_back(std::move(state.read_handler));
        state.read_handler = nullptr;
      }

      if (state.write_handler && (error_or_hangup || (revents & POLLWRNORM)))
      {
        ready_handlers.push_back(std::move(state.write_handler));
        state.write_handler = nullptr;
      }

      if (state.empty())
        descriptors_.erase(it);
    }
  }

  for (auto& handler : ready_handlers)
    handler();

  if (result > 0 && (poll_fds.back().revents & POLLRDNORM) != 0)
  {
    char buffer[64];
    while (::recv(interrupter_, buffer, sizeof(buffer), 0) > 0)
    {
    }
    interrupted_.store(false, std::memory_order_release);
  }
}

} // namespace sasio
