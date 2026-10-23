//
// ip_tcp.cpp
// ~~~~~~~~~~
//
// Two sources:
//
// 1. tcp_no_delay_test - adapted from asio/src/tests/unit/ip/tcp.cpp
//    (Copyright (c) 2003-2026 Christopher M. Kohlhoff, Boost Software License
//    1.0). The assertions come from ip_tcp_runtime::test; the parts that need
//    v6, resolvers or option archetypes are omitted.
//
// 2. tcp_echo_test / tcp_cancel_test - asio has no runtime unit test for raw
//    async socket operations (its ip/tcp.cpp only checks API surface), so
//    these are derived from src/examples/cpp11/echo/async_tcp_echo_server.cpp
//    and exercise the same accept/read/write loop against a loopback client.
//    The cases are ours, but the sequence follows the example.
//
#include "sasio/buffer.hpp"
#include "sasio/io_context.hpp"
#include "sasio/ip/tcp.hpp"
#include "sasio/steady_timer.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include "unit_test.hpp"

using namespace sasio;
namespace ip = sasio::ip;

namespace {

void write_all(ip::tcp::socket& socket, std::shared_ptr<std::string> data,
    std::function<void(error_code)> handler, std::size_t offset = 0)
{
  if (offset >= data->size())
  {
    handler(error_code());
    return;
  }

  socket.async_write_some(
      buffer(data->data() + offset, data->size() - offset),
      [&socket, data, handler, offset](error_code ec, std::size_t sent) {
        if (ec)
        {
          handler(ec);
          return;
        }
        write_all(socket, data, handler, offset + sent);
      });
}

} // namespace

//------------------------------------------------------------------------------
// Regression test for a missed wakeup: with run() blocked in WSAPoll on one
// thread, registering a read from another thread must wake it up. Without the
// interrupt in reactor::start_read/start_write the handler would only run when
// the 5 s watchdog fires.

void tcp_multithread_wakeup_test()
{
  io_context ioc;

  ip::tcp::acceptor acceptor(ioc, ip::tcp::endpoint(INADDR_LOOPBACK, 0));
  const unsigned short port = acceptor.local_endpoint().port();

  ip::tcp::socket server_socket(ioc);
  ip::tcp::socket client_socket(ioc);

  bool accepted = false;
  bool connected = false;

  acceptor.async_accept(server_socket,
      [&](error_code ec) { accepted = !ec; });
  client_socket.async_connect(ip::tcp::endpoint(INADDR_LOOPBACK, port),
      [&](error_code ec) { connected = !ec; });

  ioc.run();
  ASIO_CHECK(accepted);
  ASIO_CHECK(connected);
  ioc.restart();

  io_context::work guard(ioc);
  std::atomic<bool> read_done{false};

  steady_timer watchdog(ioc);
  watchdog.expires_after(std::chrono::seconds(5));
  watchdog.async_wait([&ioc](error_code) { ioc.stop(); });

  std::thread runner([&ioc]() { ioc.run(); });

  // Let the runner block inside WSAPoll with the current descriptor set.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  char read_buffer[16] = {};
  server_socket.async_read_some(buffer(read_buffer),
      [&](error_code, std::size_t) {
        read_done = true;
        ioc.stop();
      });

  const auto sent_at = std::chrono::steady_clock::now();
  client_socket.async_write_some(buffer("x", 1),
      [](error_code, std::size_t) {});

  runner.join();

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - sent_at).count();

  watchdog.cancel();
  guard.reset();

  ASIO_CHECK(read_done.load());
  ASIO_CHECK(elapsed < 3000);
}

//------------------------------------------------------------------------------
// Adapted from asio's ip_tcp_runtime::test (no_delay part only).

void tcp_no_delay_test()
{
  io_context ioc;
  ip::tcp::socket sock(ioc);
  sock.open();

  ip::tcp::no_delay no_delay1(true);
  ASIO_CHECK(no_delay1.value());
  sock.set_option(no_delay1);

  ip::tcp::no_delay no_delay2;
  sock.get_option(no_delay2);
  ASIO_CHECK(no_delay2.value());

  ip::tcp::no_delay no_delay3(false);
  ASIO_CHECK(!no_delay3.value());
  sock.set_option(no_delay3);

  ip::tcp::no_delay no_delay4;
  sock.get_option(no_delay4);
  ASIO_CHECK(!no_delay4.value());
}

//------------------------------------------------------------------------------
// Derived from asio's async_tcp_echo_server example: one acceptor, one
// connection, one echo round trip over loopback.

void tcp_echo_test()
{
  io_context ioc;

  ip::tcp::acceptor acceptor(ioc, ip::tcp::endpoint(INADDR_LOOPBACK, 0));
  const unsigned short port = acceptor.local_endpoint().port();

  ip::tcp::socket server_socket(ioc);
  ip::tcp::socket client_socket(ioc);

  bool accepted = false;
  bool connected = false;
  bool echoed = false;
  std::string request = "hello";
  std::string response;

  char server_buffer[64] = {};
  char client_buffer[64] = {};

  steady_timer watchdog(ioc);
  watchdog.expires_after(std::chrono::seconds(5));
  watchdog.async_wait([&ioc](error_code) { ioc.stop(); });

  acceptor.async_accept(server_socket, [&](error_code ec) {
    ASIO_CHECK(!ec);
    if (ec)
      return;

    accepted = true;
    server_socket.async_read_some(buffer(server_buffer),
        [&](error_code read_ec, std::size_t length) {
          ASIO_CHECK(!read_ec);
          if (read_ec)
            return;

          auto data = std::make_shared<std::string>(
              server_buffer, server_buffer + length);
          write_all(server_socket, data,
              [&](error_code write_ec) { ASIO_CHECK(!write_ec); });
        });
  });

  client_socket.async_connect(ip::tcp::endpoint(INADDR_LOOPBACK, port),
      [&](error_code ec) {
        ASIO_CHECK(!ec);
        if (ec)
          return;

        connected = true;
        auto data = std::make_shared<std::string>(request);
        write_all(client_socket, data, [&](error_code write_ec) {
          ASIO_CHECK(!write_ec);
          if (write_ec)
            return;

          client_socket.async_read_some(buffer(client_buffer),
              [&](error_code read_ec, std::size_t length) {
                ASIO_CHECK(!read_ec);
                if (read_ec)
                  return;

                echoed = true;
                response.assign(client_buffer, length);
                ioc.stop();
              });
        });
      });

  ioc.run();
  watchdog.cancel();

  ASIO_CHECK(accepted);
  ASIO_CHECK(connected);
  ASIO_CHECK(echoed);
  ASIO_CHECK(response == request);
}

//------------------------------------------------------------------------------
// Cancellation: a pending accept must complete with operation_aborted.

void tcp_cancel_test()
{
  io_context ioc;

  ip::tcp::acceptor acceptor(ioc, ip::tcp::endpoint(INADDR_LOOPBACK, 0));
  ip::tcp::socket peer(ioc);

  error_code result = std::make_error_code(std::errc::not_connected);
  bool completed = false;

  acceptor.async_accept(peer, [&](error_code ec) {
    completed = true;
    result = ec;
  });

  acceptor.cancel();
  ioc.run();

  ASIO_CHECK(completed);
  ASIO_CHECK(result == operation_aborted);
}

//------------------------------------------------------------------------------
// Cancellation: a pending read must complete with operation_aborted when the
// socket is closed. The sequence follows the same accept/read setup as asio's
// echo example.

void tcp_read_cancel_test()
{
  io_context ioc;

  ip::tcp::acceptor acceptor(ioc, ip::tcp::endpoint(INADDR_LOOPBACK, 0));
  const unsigned short port = acceptor.local_endpoint().port();

  ip::tcp::socket server_socket(ioc);
  ip::tcp::socket client_socket(ioc);

  char read_buffer[16] = {};
  error_code read_result = std::make_error_code(std::errc::not_connected);
  bool read_completed = false;

  steady_timer watchdog(ioc);
  watchdog.expires_after(std::chrono::seconds(5));
  watchdog.async_wait([&ioc](error_code) { ioc.stop(); });

  acceptor.async_accept(server_socket, [&](error_code ec) {
    ASIO_CHECK(!ec);
    if (ec)
      return;

    server_socket.async_read_some(buffer(read_buffer),
        [&](error_code read_ec, std::size_t) {
          read_completed = true;
          read_result = read_ec;
          ioc.stop();
        });

    // Cancel the read before any data arrives.
    server_socket.close();
  });

  client_socket.async_connect(ip::tcp::endpoint(INADDR_LOOPBACK, port),
      [](error_code ec) { ASIO_CHECK(!ec); });

  ioc.run();
  watchdog.cancel();

  ASIO_CHECK(read_completed);
  ASIO_CHECK(read_result == operation_aborted);
}

ASIO_TEST_SUITE
(
  "ip_tcp",
  ASIO_TEST_CASE(tcp_no_delay_test)
  ASIO_TEST_CASE(tcp_multithread_wakeup_test)
  ASIO_TEST_CASE(tcp_echo_test)
  ASIO_TEST_CASE(tcp_cancel_test)
  ASIO_TEST_CASE(tcp_read_cancel_test)
)
