//
// coroutine.cpp
// ~~~~~~~~~~~~~
//
// Chapter 6 tests. asio's src/tests/unit/co_spawn.cpp / awaitable.cpp are
// built around its completion-token machinery, which sasio deliberately does
// not have; these cases therefore follow asio's documented coroutine model
// rather than copying its unit tests:
//
//   * co_spawn() runs an awaitable and reports its result or exception;
//   * an adapter suspends on a timer and resumes inside the io_context;
//   * a socket session can be written as straight-line co_await code.
//
#include "sasio/awaitable.hpp"
#include "sasio/co_spawn.hpp"
#include "sasio/io_context.hpp"
#include "sasio/ip/tcp.hpp"
#include "sasio/read.hpp"
#include "sasio/steady_timer.hpp"
#include "sasio/write.hpp"

#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include "unit_test.hpp"

using namespace sasio;
namespace ip = sasio::ip;

awaitable<int> return_after_wait(steady_timer& timer)
{
  co_await async_wait(timer);
  co_return 42;
}

void coroutine_timer_test()
{
  io_context ioc;
  steady_timer timer(ioc);
  timer.expires_after(std::chrono::milliseconds(10));

  bool called = false;
  int value = 0;
  std::exception_ptr error;

  co_spawn(ioc, return_after_wait(timer),
      [&](std::exception_ptr e, int result) {
        called = true;
        error = e;
        value = result;
      });

  ioc.run();

  ASIO_CHECK(called);
  ASIO_CHECK(!error);
  ASIO_CHECK(value == 42);
}

awaitable<void> fail_immediately()
{
  throw std::runtime_error("boom");
  co_return;
}

void coroutine_exception_test()
{
  io_context ioc;

  bool called = false;
  bool caught = false;
  std::string message;

  co_spawn(ioc, fail_immediately(), [&](std::exception_ptr e) {
    called = true;
    try
    {
      if (e)
        std::rethrow_exception(e);
    }
    catch (const std::runtime_error& ex)
    {
      caught = true;
      message = ex.what();
    }
  });

  ioc.run();

  ASIO_CHECK(called);
  ASIO_CHECK(caught);
  ASIO_CHECK(message == "boom");
}

awaitable<void> echo_session(ip::tcp::acceptor& acceptor,
    ip::tcp::socket& server, char* buffer)
{
  co_await async_accept(acceptor, server);

  const std::size_t length =
      co_await async_read_some(server, sasio::buffer(buffer, 64));

  co_await async_write_some(server, sasio::buffer(buffer, length));
}

void coroutine_socket_test()
{
  io_context ioc;

  ip::tcp::acceptor acceptor(ioc, ip::tcp::endpoint(INADDR_LOOPBACK, 0));
  const unsigned short port = acceptor.local_endpoint().port();

  ip::tcp::socket server(ioc);
  ip::tcp::socket client(ioc);

  char session_buffer[64] = {};
  char reply[64] = {};
  bool echoed = false;
  bool session_finished = false;

  steady_timer watchdog(ioc);
  watchdog.expires_after(std::chrono::seconds(5));
  watchdog.async_wait([&ioc](error_code) { ioc.stop(); });

  co_spawn(ioc, echo_session(acceptor, server, session_buffer),
      [&](std::exception_ptr e) {
        ASIO_CHECK(!e);
        session_finished = true;
      });

  client.async_connect(ip::tcp::endpoint(INADDR_LOOPBACK, port),
      [&](error_code ec) {
        ASIO_CHECK(!ec);
        if (ec)
          return;

        async_write(client, sasio::buffer("hello", 5),
            [&](error_code write_ec, std::size_t written) {
              ASIO_CHECK(!write_ec);
              ASIO_CHECK(written == 5);

              async_read(client, sasio::buffer(reply, 5),
                  [&](error_code read_ec, std::size_t length) {
                    ASIO_CHECK(!read_ec);
                    ASIO_CHECK(length == 5);
                    echoed = std::memcmp(reply, "hello", 5) == 0;
                    ioc.stop();
                  });
            });
      });

  ioc.run();
  watchdog.cancel();

  ASIO_CHECK(echoed);
  ASIO_CHECK(session_finished);
}

ASIO_TEST_SUITE
(
  "coroutine",
  ASIO_TEST_CASE(coroutine_timer_test)
  ASIO_TEST_CASE(coroutine_exception_test)
  ASIO_TEST_CASE(coroutine_socket_test)
)
