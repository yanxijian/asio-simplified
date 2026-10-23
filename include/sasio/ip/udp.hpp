//
// udp.hpp
// ~~~~~~~
//
// Chapter 5: UDP. The shape follows asio's ip::udp namespace, but there is one
// concrete IPv4 socket type:
//
//   * async_send_to(buffer, endpoint, handler)
//   * async_receive_from(buffer, endpoint&, handler)
//
// asio's udp::endpoint is a distinct type; here it is the same IPv4
// address/port pair as tcp::endpoint, which is enough for one protocol family.
//
// asio counterpart: asio/ip/udp.hpp, asio/basic_datagram_socket.hpp
//
#ifndef SASIO_IP_UDP_HPP
#define SASIO_IP_UDP_HPP

#include "sasio/buffer.hpp"
#include "sasio/config.hpp"
#include "sasio/error.hpp"
#include "sasio/ip/tcp.hpp"

#include <cstddef>
#include <functional>
#include <utility>

namespace sasio {

class io_context;
class reactor;
class scheduler;

namespace ip {
namespace udp {

using endpoint = tcp::endpoint;

class socket
{
public:
  explicit socket(io_context& ctx);
  ~socket();

  socket(const socket&) = delete;
  socket& operator=(const socket&) = delete;

  void open();
  void close();
  void cancel();
  bool is_open() const { return handle_ != INVALID_SOCKET; }

  void bind(const endpoint& ep);
  endpoint local_endpoint() const;

  template <typename Handler>
  void async_send_to(const_buffer buffer, const endpoint& destination,
      Handler&& handler)
  {
    start_send_to(buffer, destination,
        std::function<void(error_code, std::size_t)>(
            std::forward<Handler>(handler)));
  }

  template <typename Handler>
  void async_receive_from(mutable_buffer buffer, endpoint& sender,
      Handler&& handler)
  {
    start_receive_from(buffer, sender,
        std::function<void(error_code, std::size_t)>(
            std::forward<Handler>(handler)));
  }

  SOCKET native_handle() const { return handle_; }
  io_context& context() const { return context_; }

private:
  void start_send_to(const_buffer buffer, const endpoint& destination,
      std::function<void(error_code, std::size_t)> handler);
  void start_receive_from(mutable_buffer buffer, endpoint& sender,
      std::function<void(error_code, std::size_t)> handler);

  void do_send();
  void do_receive();
  void complete_send(error_code ec, std::size_t bytes);
  void complete_receive(error_code ec, std::size_t bytes);

  io_context& context_;
  reactor& reactor_;
  scheduler& scheduler_;
  SOCKET handle_ = INVALID_SOCKET;

  bool send_pending_ = false;
  std::function<void(error_code, std::size_t)> send_handler_;
  const_buffer send_buffer_;
  endpoint send_destination_;

  bool receive_pending_ = false;
  std::function<void(error_code, std::size_t)> receive_handler_;
  mutable_buffer receive_buffer_;
  endpoint* receive_sender_ = nullptr;
};

} // namespace udp
} // namespace ip
} // namespace sasio

#endif // SASIO_IP_UDP_HPP
