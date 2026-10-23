//
// tcp.hpp
// ~~~~~~~
//
// Chapter 2: the smallest useful TCP layer.
//
// asio has basic_socket, basic_stream_socket, basic_socket_acceptor and a
// resolver, each templated on protocol and executor. Here there is one
// concrete protocol (IPv4/TCP), one concrete socket type and one concrete
// acceptor; the names still follow asio's tcp namespace.
//
// Deliberate simplifications (all documented in README):
//   * IPv4 only, no resolver;
//   * one outstanding operation per direction (no operation queue);
//   * no executors, no socket options beyond no_delay;
//   * async_accept() takes a socket&, so socket does not need move semantics.
//
// asio counterpart: asio/ip/tcp.hpp, asio/basic_stream_socket.hpp,
//                   asio/basic_socket_acceptor.hpp
//
#ifndef SASIO_IP_TCP_HPP
#define SASIO_IP_TCP_HPP

#include "sasio/buffer.hpp"
#include "sasio/config.hpp"
#include "sasio/error.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

namespace sasio {

class io_context;
class reactor;
class scheduler;

namespace ip {
namespace tcp {

// An IPv4 address/port pair. The address is stored in host byte order so that
// endpoint(0x7F000001, 8080) reads as 127.0.0.1:8080; conversion to network
// order happens at the socket_ops boundary.
class endpoint
{
public:
  endpoint() : address_(0), port_(0) {}
  explicit endpoint(unsigned short port) : address_(0), port_(port) {}
  endpoint(unsigned long ipv4_address, unsigned short port)
    : address_(ipv4_address), port_(port) {}

  unsigned long address() const { return address_; }
  unsigned short port() const { return port_; }

  std::string to_string() const;

private:
  unsigned long address_;
  unsigned short port_;
};

// asio::ip::tcp::no_delay, reduced to the two operations the tests need.
class no_delay
{
public:
  explicit no_delay(bool value = true) : value_(value) {}

  bool value() const { return value_; }
  void value(bool value) { value_ = value; }

private:
  bool value_;
};

enum shutdown_type
{
  shutdown_receive = SD_RECEIVE,
  shutdown_send = SD_SEND,
  shutdown_both = SD_BOTH
};

class socket
{
public:
  explicit socket(io_context& ctx);
  ~socket();

  socket(const socket&) = delete;
  socket& operator=(const socket&) = delete;

  void open();
  void close();
  bool is_open() const { return handle_ != INVALID_SOCKET; }
  void cancel();

  void bind(const endpoint& ep);
  void shutdown(shutdown_type what);

  void set_option(const no_delay& option);
  void get_option(no_delay& option) const;

  endpoint local_endpoint() const;
  endpoint remote_endpoint() const;

  template <typename Handler>
  void async_connect(const endpoint& ep, Handler&& handler)
  {
    start_connect(ep, std::function<void(error_code)>(
            std::forward<Handler>(handler)));
  }

  template <typename Handler>
  void async_read_some(mutable_buffer buffer, Handler&& handler)
  {
    start_read_some(buffer, std::function<void(error_code, std::size_t)>(
            std::forward<Handler>(handler)));
  }

  template <typename Handler>
  void async_write_some(const_buffer buffer, Handler&& handler)
  {
    start_write_some(buffer, std::function<void(error_code, std::size_t)>(
            std::forward<Handler>(handler)));
  }

  SOCKET native_handle() const { return handle_; }

private:
  friend class acceptor;

  // Adopts a freshly accepted descriptor. Called by acceptor only.
  void assign(SOCKET handle);

  void start_connect(const endpoint& ep,
      std::function<void(error_code)> handler);
  void start_read_some(mutable_buffer buffer,
      std::function<void(error_code, std::size_t)> handler);
  void start_write_some(const_buffer buffer,
      std::function<void(error_code, std::size_t)> handler);

  void do_connect();
  void do_read();
  void do_write();

  void complete_connect(error_code ec);
  void complete_read(error_code ec, std::size_t bytes);
  void complete_write(error_code ec, std::size_t bytes);

  io_context& context_;
  reactor& reactor_;
  scheduler& scheduler_;
  SOCKET handle_ = INVALID_SOCKET;

  bool connect_pending_ = false;
  std::function<void(error_code)> connect_handler_;

  bool read_pending_ = false;
  std::function<void(error_code, std::size_t)> read_handler_;
  mutable_buffer read_buffer_;

  bool write_pending_ = false;
  std::function<void(error_code, std::size_t)> write_handler_;
  const_buffer write_buffer_;
};

class acceptor
{
public:
  explicit acceptor(io_context& ctx);
  acceptor(io_context& ctx, const endpoint& ep, bool reuse_address = true);
  ~acceptor();

  acceptor(const acceptor&) = delete;
  acceptor& operator=(const acceptor&) = delete;

  void open();
  void bind(const endpoint& ep);
  void listen(int backlog = 32);
  void close();
  void cancel();

  bool is_open() const { return handle_ != INVALID_SOCKET; }
  endpoint local_endpoint() const;
  SOCKET native_handle() const { return handle_; }

  template <typename Handler>
  void async_accept(socket& peer, Handler&& handler)
  {
    start_accept(peer, std::function<void(error_code)>(
            std::forward<Handler>(handler)));
  }

private:
  void start_accept(socket& peer, std::function<void(error_code)> handler);
  void do_accept();
  void complete_accept(error_code ec);

  io_context& context_;
  reactor& reactor_;
  scheduler& scheduler_;
  SOCKET handle_ = INVALID_SOCKET;

  bool accept_pending_ = false;
  socket* peer_ = nullptr;
  std::function<void(error_code)> accept_handler_;
};

} // namespace tcp
} // namespace ip
} // namespace sasio

#endif // SASIO_IP_TCP_HPP
