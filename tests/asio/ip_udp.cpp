//
// ip_udp.cpp
// ~~~~~~~~~~
//
// Derived from asio's src/examples/cpp11/echo/async_udp_echo_server.cpp.
//
// asio's src/tests/unit/ip/udp.cpp only checks the API surface (options and
// handler archetypes), not real datagram transfer, so this loopback echo test
// follows the example instead.
//
#include "sasio/buffer.hpp"
#include "sasio/io_context.hpp"
#include "sasio/ip/udp.hpp"
#include "sasio/steady_timer.hpp"

#include <chrono>
#include <cstring>
#include "unit_test.hpp"

using namespace sasio;
namespace ip = sasio::ip;

void udp_echo_test()
{
  io_context ioc;

  ip::udp::socket server(ioc);
  server.open();
  server.bind(ip::udp::endpoint(INADDR_LOOPBACK, 0));
  const unsigned short port = server.local_endpoint().port();

  ip::udp::socket client(ioc);
  client.open();
  client.bind(ip::udp::endpoint(INADDR_LOOPBACK, 0));

  char server_buffer[64] = {};
  char client_buffer[64] = {};
  ip::udp::endpoint sender;
  ip::udp::endpoint from;
  bool echoed = false;

  steady_timer watchdog(ioc);
  watchdog.expires_after(std::chrono::seconds(5));
  watchdog.async_wait([&ioc](error_code) { ioc.stop(); });

  server.async_receive_from(buffer(server_buffer), sender,
      [&](error_code ec, std::size_t length) {
        ASIO_CHECK(!ec);
        if (ec)
          return;

        server.async_send_to(buffer(server_buffer, length), sender,
            [](error_code send_ec, std::size_t sent) {
              ASIO_CHECK(!send_ec);
              ASIO_CHECK(sent > 0);
            });
      });

  client.async_send_to(buffer("hello", 5),
      ip::udp::endpoint(INADDR_LOOPBACK, port),
      [&](error_code ec, std::size_t sent) {
        ASIO_CHECK(!ec);
        ASIO_CHECK(sent == 5);

        client.async_receive_from(buffer(client_buffer), from,
            [&](error_code recv_ec, std::size_t length) {
              ASIO_CHECK(!recv_ec);
              ASIO_CHECK(length == 5);
              echoed = std::memcmp(client_buffer, "hello", 5) == 0;
              ioc.stop();
            });
      });

  ioc.run();
  watchdog.cancel();

  ASIO_CHECK(echoed);
}

ASIO_TEST_SUITE
(
  "ip_udp",
  ASIO_TEST_CASE(udp_echo_test)
)
