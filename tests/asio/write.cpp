//
// write.cpp
// ~~~~~~~~~
//
// Adapted from asio/src/tests/unit/write.cpp
// (Copyright (c) 2003-2026 Christopher M. Kohlhoff,
//  distributed under the Boost Software License 1.0).
//
// Adaptations:
//   * asio::async_write -> sasio::async_write, with a single const_buffer
//   * the boost::array / std::array / vector / dynamic buffer / streambuf
//     overloads are OMITTED: sasio has no buffer sequences yet
//   * the synchronous write() tests and the lazy/archetype handler forms are
//     OMITTED as well
//
// What remains is asio's own partial-write test: the stream returns 1 or 10
// bytes at a time, and the composed operation must still write everything.
//
#include "sasio/io_context.hpp"
#include "sasio/write.hpp"

#include <cstring>
#include <functional>
#include "test_stream.hpp"
#include "unit_test.hpp"

using namespace sasio;

static const char write_data[]
  = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

void async_write_handler(const error_code& e, std::size_t bytes_transferred,
    std::size_t expected_bytes_transferred, bool* called)
{
  *called = true;
  ASIO_CHECK(!e);
  ASIO_CHECK(bytes_transferred == expected_bytes_transferred);
}

void test_3_arg_const_buffer_async_write()
{
  io_context ioc;
  test_stream s(ioc);
  const_buffer buffers = buffer(write_data, sizeof(write_data));

  s.reset();
  bool called = false;
  async_write(s, buffers,
      std::bind(async_write_handler,
        std::placeholders::_1, std::placeholders::_2,
        sizeof(write_data), &called));
  ioc.restart();
  ioc.run();
  ASIO_CHECK(called);
  ASIO_CHECK(s.check_buffers(buffers, sizeof(write_data)));

  s.reset();
  s.next_write_length(1);
  called = false;
  async_write(s, buffers,
      std::bind(async_write_handler,
        std::placeholders::_1, std::placeholders::_2,
        sizeof(write_data), &called));
  ioc.restart();
  ioc.run();
  ASIO_CHECK(called);
  ASIO_CHECK(s.check_buffers(buffers, sizeof(write_data)));

  s.reset();
  s.next_write_length(10);
  called = false;
  async_write(s, buffers,
      std::bind(async_write_handler,
        std::placeholders::_1, std::placeholders::_2,
        sizeof(write_data), &called));
  ioc.restart();
  ioc.run();
  ASIO_CHECK(called);
  ASIO_CHECK(s.check_buffers(buffers, sizeof(write_data)));
}

ASIO_TEST_SUITE
(
  "write",
  ASIO_TEST_CASE(test_3_arg_const_buffer_async_write)
)
