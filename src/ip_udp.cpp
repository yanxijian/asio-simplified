#include "sasio/ip/udp.hpp"

#include "sasio/detail/socket_ops.hpp"
#include "sasio/io_context.hpp"
#include "sasio/reactor.hpp"
#include "sasio/scheduler.hpp"

#include <algorithm>
#include <climits>
#include <system_error>
#include <utility>

namespace sasio {
namespace ip {
namespace udp {
namespace {

sockaddr_in to_sockaddr(const endpoint& ep)
{
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(ep.address());
  address.sin_port = htons(ep.port());
  return address;
}

endpoint from_sockaddr(const sockaddr_in& address)
{
  return endpoint(ntohl(address.sin_addr.s_addr),
      ntohs(address.sin_port));
}

void throw_error(const error_code& ec, const char* what)
{
  if (ec)
    throw std::system_error(ec, what);
}

std::size_t clamp_to_int(std::size_t size)
{
  return (std::min)(size, static_cast<std::size_t>(INT_MAX));
}

} // namespace

socket::socket(io_context& ctx)
  : context_(ctx),
    reactor_(ctx.use_service<reactor>()),
    scheduler_(ctx.use_service<scheduler>())
{
}

socket::~socket()
{
  close();
}

void socket::open()
{
  if (is_open())
    return;

  error_code ec;
  const SOCKET handle = detail::socket_ops::create_datagram_socket(ec);
  throw_error(ec, "udp::socket::open");

  if (!detail::socket_ops::set_non_blocking(handle, ec))
  {
    detail::socket_ops::close_socket(handle);
    throw_error(ec, "udp::socket::open: non-blocking mode");
  }

  handle_ = handle;
}

void socket::close()
{
  if (!is_open())
    return;

  cancel();
  reactor_.close_descriptor(handle_);
  detail::socket_ops::close_socket(handle_);
  handle_ = INVALID_SOCKET;
}

void socket::cancel()
{
  if (!is_open())
    return;

  if (send_pending_)
    complete_send(operation_aborted, 0);
  if (receive_pending_)
    complete_receive(operation_aborted, 0);
}

void socket::bind(const endpoint& ep)
{
  if (!is_open())
    open();

  error_code ec;
  if (!detail::socket_ops::bind_socket(handle_, to_sockaddr(ep), ec))
    throw_error(ec, "udp::socket::bind");
}

endpoint socket::local_endpoint() const
{
  sockaddr_in address = {};
  error_code ec;
  if (!detail::socket_ops::local_endpoint(handle_, address, ec))
    throw_error(ec, "udp::socket::local_endpoint");
  return from_sockaddr(address);
}

void socket::start_send_to(const_buffer buffer, const endpoint& destination,
    std::function<void(error_code, std::size_t)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor, 0);
    });
    return;
  }

  if (send_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started, 0);
    });
    return;
  }

  send_pending_ = true;
  send_buffer_ = buffer;
  send_destination_ = destination;
  send_handler_ = std::move(handler);
  reactor_.start_write(handle_, [this]() { do_send(); });
}

void socket::do_send()
{
  if (!send_pending_)
    return;

  error_code ec;
  const int result = detail::socket_ops::send_to_socket(handle_,
      send_buffer_.data(), clamp_to_int(send_buffer_.size()),
      to_sockaddr(send_destination_), ec);

  if (result < 0)
  {
    if (ec == would_block)
    {
      reactor_.start_write(handle_, [this]() { do_send(); });
      return;
    }

    complete_send(ec, 0);
    return;
  }

  complete_send(error_code(), static_cast<std::size_t>(result));
}

void socket::complete_send(error_code ec, std::size_t bytes)
{
  send_pending_ = false;
  reactor_.stop_write(handle_);

  std::function<void(error_code, std::size_t)> handler =
      std::move(send_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec, bytes]() mutable {
    handler(ec, bytes);
  });
}

void socket::start_receive_from(mutable_buffer buffer, endpoint& sender,
    std::function<void(error_code, std::size_t)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor, 0);
    });
    return;
  }

  if (receive_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started, 0);
    });
    return;
  }

  receive_pending_ = true;
  receive_buffer_ = buffer;
  receive_sender_ = &sender;
  receive_handler_ = std::move(handler);
  reactor_.start_read(handle_, [this]() { do_receive(); });
}

void socket::do_receive()
{
  if (!receive_pending_)
    return;

  sockaddr_in address = {};
  error_code ec;
  const int result = detail::socket_ops::recv_from_socket(handle_,
      receive_buffer_.data(), clamp_to_int(receive_buffer_.size()),
      address, ec);

  if (result < 0)
  {
    if (ec == would_block)
    {
      reactor_.start_read(handle_, [this]() { do_receive(); });
      return;
    }

    complete_receive(ec, 0);
    return;
  }

  if (receive_sender_)
    *receive_sender_ = from_sockaddr(address);

  complete_receive(error_code(), static_cast<std::size_t>(result));
}

void socket::complete_receive(error_code ec, std::size_t bytes)
{
  receive_pending_ = false;
  receive_sender_ = nullptr;
  reactor_.stop_read(handle_);

  std::function<void(error_code, std::size_t)> handler =
      std::move(receive_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec, bytes]() mutable {
    handler(ec, bytes);
  });
}

} // namespace udp
} // namespace ip
} // namespace sasio
