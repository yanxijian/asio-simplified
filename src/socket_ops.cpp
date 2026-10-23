#include "sasio/detail/socket_ops.hpp"

namespace sasio {
namespace detail {
namespace socket_ops {
namespace {

error_code make_error(int code)
{
  if (code == WSAEWOULDBLOCK)
    return would_block;
  return error_code(code, std::system_category());
}

bool failed(int result, error_code& ec)
{
  if (result != SOCKET_ERROR)
  {
    ec.clear();
    return false;
  }

  ec = make_error(::WSAGetLastError());
  return true;
}

bool failed_socket(SOCKET result, error_code& ec)
{
  if (result != INVALID_SOCKET)
  {
    ec.clear();
    return false;
  }

  ec = make_error(::WSAGetLastError());
  return true;
}

} // namespace

SOCKET create_stream_socket(error_code& ec)
{
  const SOCKET result = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (result == INVALID_SOCKET)
    ec = last_socket_error();
  else
    ec.clear();
  return result;
}

SOCKET create_datagram_socket(error_code& ec)
{
  const SOCKET result = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (result == INVALID_SOCKET)
    ec = last_socket_error();
  else
    ec.clear();
  return result;
}

bool set_non_blocking(SOCKET descriptor, error_code& ec)
{
  u_long non_blocking = 1;
  return !failed(::ioctlsocket(descriptor, FIONBIO, &non_blocking), ec);
}

bool set_reuse_address(SOCKET descriptor, bool value, error_code& ec)
{
  const BOOL option = value ? TRUE : FALSE;
  return !failed(::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR,
          reinterpret_cast<const char*>(&option), sizeof(option)), ec);
}

bool bind_socket(SOCKET descriptor, const sockaddr_in& address, error_code& ec)
{
  return !failed(::bind(descriptor,
          reinterpret_cast<const sockaddr*>(&address), sizeof(address)), ec);
}

bool listen_socket(SOCKET descriptor, int backlog, error_code& ec)
{
  return !failed(::listen(descriptor, backlog), ec);
}

bool connect_socket(SOCKET descriptor, const sockaddr_in& address,
    error_code& ec)
{
  // On a non-blocking socket a successful start returns SOCKET_ERROR with
  // WSAEWOULDBLOCK; that is reported as would_block and is not a failure.
  return !failed(::connect(descriptor,
          reinterpret_cast<const sockaddr*>(&address), sizeof(address)), ec);
}

SOCKET accept_socket(SOCKET descriptor, sockaddr_in& address, error_code& ec)
{
  int length = sizeof(address);
  const SOCKET result = ::accept(descriptor,
      reinterpret_cast<sockaddr*>(&address), &length);
  if (result == INVALID_SOCKET)
    ec = make_error(::WSAGetLastError());
  else
    ec.clear();
  return result;
}

int recv_socket(SOCKET descriptor, void* data, std::size_t size, error_code& ec)
{
  const int result = ::recv(descriptor, static_cast<char*>(data),
      static_cast<int>(size), 0);
  if (result == SOCKET_ERROR)
    ec = make_error(::WSAGetLastError());
  else
    ec.clear();
  return result;
}

int send_socket(SOCKET descriptor, const void* data, std::size_t size,
    error_code& ec)
{
  const int result = ::send(descriptor, static_cast<const char*>(data),
      static_cast<int>(size), 0);
  if (result == SOCKET_ERROR)
    ec = make_error(::WSAGetLastError());
  else
    ec.clear();
  return result;
}

int recv_from_socket(SOCKET descriptor, void* data, std::size_t size,
    sockaddr_in& address, error_code& ec)
{
  int length = sizeof(address);
  const int result = ::recvfrom(descriptor, static_cast<char*>(data),
      static_cast<int>(size), 0,
      reinterpret_cast<sockaddr*>(&address), &length);
  if (result == SOCKET_ERROR)
    ec = make_error(::WSAGetLastError());
  else
    ec.clear();
  return result;
}

int send_to_socket(SOCKET descriptor, const void* data, std::size_t size,
    const sockaddr_in& address, error_code& ec)
{
  const int result = ::sendto(descriptor, static_cast<const char*>(data),
      static_cast<int>(size), 0,
      reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  if (result == SOCKET_ERROR)
    ec = make_error(::WSAGetLastError());
  else
    ec.clear();
  return result;
}

bool shutdown_socket(SOCKET descriptor, int how, error_code& ec)
{
  return !failed(::shutdown(descriptor, how), ec);
}

bool get_socket_error(SOCKET descriptor, error_code& ec)
{
  int error = 0;
  int length = sizeof(error);
  if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR,
          reinterpret_cast<char*>(&error), &length) == SOCKET_ERROR)
  {
    ec = last_socket_error();
    return false;
  }

  ec = error != 0 ? make_error(error) : error_code();
  return true;
}

bool local_endpoint(SOCKET descriptor, sockaddr_in& address, error_code& ec)
{
  int length = sizeof(address);
  return !failed(::getsockname(descriptor,
          reinterpret_cast<sockaddr*>(&address), &length), ec);
}

bool remote_endpoint(SOCKET descriptor, sockaddr_in& address, error_code& ec)
{
  int length = sizeof(address);
  return !failed(::getpeername(descriptor,
          reinterpret_cast<sockaddr*>(&address), &length), ec);
}

void close_socket(SOCKET descriptor)
{
  if (descriptor != INVALID_SOCKET)
    ::closesocket(descriptor);
}

} // namespace socket_ops
} // namespace detail
} // namespace sasio
