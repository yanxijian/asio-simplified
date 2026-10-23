//
// resolver.cpp
// ~~~~~~~~~~~~
//
// asio's src/tests/unit/ip/basic_resolver.cpp is an API-surface test (it needs
// the resolver service and handler archetypes). These cases therefore check
// the behaviour asio documents: resolve("localhost") yields the loopback
// address, synchronously and asynchronously.
//
#include "sasio/io_context.hpp"
#include "sasio/ip/resolver.hpp"

#include <cstring>
#include "unit_test.hpp"

using namespace sasio;
namespace ip = sasio::ip;

void resolver_sync_test()
{
  io_context ioc;
  ip::resolver r(ioc);

  ip::resolver::results results =
      r.resolve(ip::resolver::query("localhost", "80"));

  ASIO_CHECK(!results.empty());

  bool loopback = false;
  for (const auto& ep : results)
  {
    if (ep.address() == INADDR_LOOPBACK)
      loopback = true;
  }

  ASIO_CHECK(loopback);
}

void resolver_async_test()
{
  io_context ioc;
  ip::resolver r(ioc);

  bool called = false;
  bool loopback = false;
  error_code result = std::make_error_code(std::errc::not_connected);

  r.async_resolve(ip::resolver::query("localhost", "80"),
      [&](error_code ec, ip::resolver::results results) {
        called = true;
        result = ec;
        for (const auto& ep : results)
        {
          if (ep.address() == INADDR_LOOPBACK)
            loopback = true;
        }
      });

  ioc.run();

  ASIO_CHECK(called);
  ASIO_CHECK(!result);
  ASIO_CHECK(loopback);
}

ASIO_TEST_SUITE
(
  "resolver",
  ASIO_TEST_CASE(resolver_sync_test)
  ASIO_TEST_CASE(resolver_async_test)
)
