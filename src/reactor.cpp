#include "sasio/reactor.hpp"

#include "sasio/error.hpp"

#include <climits>
#include <system_error>

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

void reactor::run_for(clock::duration timeout)
{
  if (interrupter_ == INVALID_SOCKET)
  {
    // Context is shutting down. Sleep instead of spinning: the scheduler will
    // see the stop flag as soon as it can run again.
    return;
  }

  WSAPOLLFD descriptor = {};
  descriptor.fd = interrupter_;
  descriptor.events = POLLRDNORM;
  descriptor.revents = 0;

  const int result = ::WSAPoll(&descriptor, 1, to_milliseconds(timeout));

  if (result == SOCKET_ERROR)
  {
    const error_code error = last_socket_error();
    if (error.value() == WSAEINTR)
      return;
    throw std::system_error(error, "WSAPoll() failed");
  }

  if (result > 0 && (descriptor.revents & POLLRDNORM) != 0)
  {
    char buffer[64];
    while (::recv(interrupter_, buffer, sizeof(buffer), 0) > 0)
    {
    }
    interrupted_.store(false, std::memory_order_release);
  }
}

} // namespace sasio
