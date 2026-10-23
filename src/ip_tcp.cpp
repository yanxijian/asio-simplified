#include "sasio/ip/tcp.hpp"

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
namespace tcp {
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

std::string endpoint::to_string() const
{
  in_addr address = {};
  address.s_addr = htonl(address_);

  char text[INET_ADDRSTRLEN] = {};
  ::inet_ntop(AF_INET, &address, text, sizeof(text));

  return std::string(text) + ":" + std::to_string(port_);
}

//------------------------------------------------------------------------------
// socket

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
  const SOCKET handle = detail::socket_ops::create_stream_socket(ec);
  throw_error(ec, "socket::open");

  if (!detail::socket_ops::set_non_blocking(handle, ec))
  {
    detail::socket_ops::close_socket(handle);
    throw_error(ec, "socket::open: non-blocking mode");
  }

  handle_ = handle;
}

void socket::assign(SOCKET handle)
{
  if (is_open())
    close();

  // The acceptor has already put the descriptor into non-blocking mode.
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

  if (connect_pending_)
    complete_connect(operation_aborted);
  if (read_pending_)
    complete_read(operation_aborted, 0);
  if (write_pending_)
    complete_write(operation_aborted, 0);
}

void socket::bind(const endpoint& ep)
{
  if (!is_open())
    open();

  error_code ec;
  if (!detail::socket_ops::bind_socket(handle_, to_sockaddr(ep), ec))
    throw_error(ec, "socket::bind");
}

void socket::shutdown(shutdown_type what)
{
  error_code ec;
  if (!detail::socket_ops::shutdown_socket(handle_, what, ec))
    throw_error(ec, "socket::shutdown");
}

void socket::set_option(const no_delay& option)
{
  const BOOL value = option.value() ? TRUE : FALSE;
  if (::setsockopt(handle_, IPPROTO_TCP, TCP_NODELAY,
          reinterpret_cast<const char*>(&value), sizeof(value)) == SOCKET_ERROR)
    throw std::system_error(last_socket_error(), "socket::set_option");
}

void socket::get_option(no_delay& option) const
{
  BOOL value = FALSE;
  int length = sizeof(value);
  if (::getsockopt(handle_, IPPROTO_TCP, TCP_NODELAY,
          reinterpret_cast<char*>(&value), &length) == SOCKET_ERROR)
    throw std::system_error(last_socket_error(), "socket::get_option");

  option.value(value != FALSE);
}

endpoint socket::local_endpoint() const
{
  sockaddr_in address = {};
  error_code ec;
  if (!detail::socket_ops::local_endpoint(handle_, address, ec))
    throw_error(ec, "socket::local_endpoint");
  return from_sockaddr(address);
}

endpoint socket::remote_endpoint() const
{
  sockaddr_in address = {};
  error_code ec;
  if (!detail::socket_ops::remote_endpoint(handle_, address, ec))
    throw_error(ec, "socket::remote_endpoint");
  return from_sockaddr(address);
}

void socket::start_connect(const endpoint& ep,
    std::function<void(error_code)> handler)
{
  if (!is_open())
    open();

  if (connect_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started);
    });
    return;
  }

  connect_pending_ = true;
  connect_handler_ = std::move(handler);

  error_code ec;
  if (!detail::socket_ops::connect_socket(handle_, to_sockaddr(ep), ec))
  {
    if (ec == would_block)
    {
      // Connection is in progress: wait until the descriptor is writable.
      reactor_.start_write(handle_, [this]() { do_connect(); });
      return;
    }

    complete_connect(ec);
    return;
  }

  complete_connect(error_code());
}

void socket::do_connect()
{
  if (!connect_pending_)
    return;

  error_code ec;
  detail::socket_ops::get_socket_error(handle_, ec);
  complete_connect(ec);
}

void socket::complete_connect(error_code ec)
{
  connect_pending_ = false;
  reactor_.stop_write(handle_);

  std::function<void(error_code)> handler = std::move(connect_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec]() mutable {
    handler(ec);
  });
}

void socket::start_read_some(mutable_buffer buffer,
    std::function<void(error_code, std::size_t)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor, 0);
    });
    return;
  }

  if (read_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started, 0);
    });
    return;
  }

  read_pending_ = true;
  read_buffer_ = buffer;
  read_handler_ = std::move(handler);
  reactor_.start_read(handle_, [this]() { do_read(); });
}

void socket::do_read()
{
  if (!read_pending_)
    return;

  error_code ec;
  const int result = detail::socket_ops::recv_socket(handle_,
      read_buffer_.data(), clamp_to_int(read_buffer_.size()), ec);

  if (result < 0)
  {
    if (ec == would_block)
    {
      // Spurious readiness: register again and wait.
      reactor_.start_read(handle_, [this]() { do_read(); });
      return;
    }

    complete_read(ec, 0);
    return;
  }

  complete_read(error_code(), static_cast<std::size_t>(result));
}

void socket::complete_read(error_code ec, std::size_t bytes)
{
  read_pending_ = false;
  reactor_.stop_read(handle_);

  std::function<void(error_code, std::size_t)> handler =
      std::move(read_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec, bytes]() mutable {
    handler(ec, bytes);
  });
}

void socket::start_write_some(const_buffer buffer,
    std::function<void(error_code, std::size_t)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor, 0);
    });
    return;
  }

  if (write_pending_ || connect_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started, 0);
    });
    return;
  }

  write_pending_ = true;
  write_buffer_ = buffer;
  write_handler_ = std::move(handler);
  reactor_.start_write(handle_, [this]() { do_write(); });
}

void socket::do_write()
{
  if (!write_pending_)
    return;

  error_code ec;
  const int result = detail::socket_ops::send_socket(handle_,
      write_buffer_.data(), clamp_to_int(write_buffer_.size()), ec);

  if (result < 0)
  {
    if (ec == would_block)
    {
      reactor_.start_write(handle_, [this]() { do_write(); });
      return;
    }

    complete_write(ec, 0);
    return;
  }

  complete_write(error_code(), static_cast<std::size_t>(result));
}

void socket::complete_write(error_code ec, std::size_t bytes)
{
  write_pending_ = false;
  reactor_.stop_write(handle_);

  std::function<void(error_code, std::size_t)> handler =
      std::move(write_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec, bytes]() mutable {
    handler(ec, bytes);
  });
}

//------------------------------------------------------------------------------
// acceptor

acceptor::acceptor(io_context& ctx)
  : context_(ctx),
    reactor_(ctx.use_service<reactor>()),
    scheduler_(ctx.use_service<scheduler>())
{
}

acceptor::acceptor(io_context& ctx, const endpoint& ep, bool reuse_address)
  : acceptor(ctx)
{
  open();

  if (reuse_address)
  {
    error_code ec;
    if (!detail::socket_ops::set_reuse_address(handle_, true, ec))
    {
      detail::socket_ops::close_socket(handle_);
      handle_ = INVALID_SOCKET;
      throw_error(ec, "acceptor: reuse_address");
    }
  }

  bind(ep);
  listen();
}

acceptor::~acceptor()
{
  close();
}

void acceptor::open()
{
  if (is_open())
    return;

  error_code ec;
  const SOCKET handle = detail::socket_ops::create_stream_socket(ec);
  throw_error(ec, "acceptor::open");

  if (!detail::socket_ops::set_non_blocking(handle, ec))
  {
    detail::socket_ops::close_socket(handle);
    throw_error(ec, "acceptor::open: non-blocking mode");
  }

  handle_ = handle;
}

void acceptor::bind(const endpoint& ep)
{
  if (!is_open())
    open();

  error_code ec;
  if (!detail::socket_ops::bind_socket(handle_, to_sockaddr(ep), ec))
    throw_error(ec, "acceptor::bind");
}

void acceptor::listen(int backlog)
{
  if (!is_open())
    open();

  error_code ec;
  if (!detail::socket_ops::listen_socket(handle_, backlog, ec))
    throw_error(ec, "acceptor::listen");
}

endpoint acceptor::local_endpoint() const
{
  sockaddr_in address = {};
  error_code ec;
  if (!detail::socket_ops::local_endpoint(handle_, address, ec))
    throw_error(ec, "acceptor::local_endpoint");
  return from_sockaddr(address);
}

void acceptor::close()
{
  if (!is_open())
    return;

  cancel();
  reactor_.close_descriptor(handle_);
  detail::socket_ops::close_socket(handle_);
  handle_ = INVALID_SOCKET;
}

void acceptor::cancel()
{
  if (!is_open() || !accept_pending_)
    return;

  complete_accept(operation_aborted);
}

void acceptor::start_accept(socket& peer,
    std::function<void(error_code)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor);
    });
    return;
  }

  if (accept_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started);
    });
    return;
  }

  accept_pending_ = true;
  peer_ = &peer;
  accept_handler_ = std::move(handler);
  reactor_.start_read(handle_, [this]() { do_accept(); });
}

void acceptor::do_accept()
{
  if (!accept_pending_)
    return;

  sockaddr_in address = {};
  error_code ec;
  const SOCKET accepted =
      detail::socket_ops::accept_socket(handle_, address, ec);

  if (accepted == INVALID_SOCKET)
  {
    if (ec == would_block)
    {
      reactor_.start_read(handle_, [this]() { do_accept(); });
      return;
    }

    complete_accept(ec);
    return;
  }

  if (!detail::socket_ops::set_non_blocking(accepted, ec))
  {
    detail::socket_ops::close_socket(accepted);
    complete_accept(ec);
    return;
  }

  peer_->assign(accepted);
  complete_accept(error_code());
}

void acceptor::complete_accept(error_code ec)
{
  accept_pending_ = false;
  peer_ = nullptr;
  reactor_.stop_read(handle_);

  std::function<void(error_code)> handler = std::move(accept_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec]() mutable {
    handler(ec);
  });
}

} // namespace tcp
} // namespace ip
} // namespace sasio
