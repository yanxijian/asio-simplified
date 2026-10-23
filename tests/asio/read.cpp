//
// read.cpp
// ~~~~~~~~
//
// Adapted from asio/src/tests/unit/read.cpp
// (Copyright (c) 2003-2026 Christopher M. Kohlhoff,
//  distributed under the Boost Software License 1.0).
//
// Adaptations:
//   * asio::async_read -> sasio::async_read, with a single mutable_buffer
//   * the array / vector / dynamic buffer / streambuf overloads are OMITTED
//   * the synchronous read() tests and the lazy/archetype handler forms are
//     OMITTED as well
//
// What remains is asio's own partial-read test: the stream delivers 1 or 10
// bytes at a time, and the composed operation must still fill the buffer.
//
#include "sasio/io_context.hpp"
#include "sasio/read.hpp"

#include <cstring>
#include <functional>
#include "test_stream.hpp"
#include "unit_test.hpp"

using namespace sasio;

static const char read_data[]
  = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

void async_read_handler(const error_code& e, std::size_t bytes_transferred,
    std::size_t expected_bytes_transferred, bool* called)
{
  *called = true;
  ASIO_CHECK(!e);
  ASIO_CHECK(bytes_transferred == expected_bytes_transferred);
}

void test_3_arg_mutable_buffer_async_read()
{
  io_context ioc;
  test_stream s(ioc);
  char read_buf[sizeof(read_data)];
  mutable_buffer buffers = buffer(read_buf, sizeof(read_buf));

  s.reset(read_data, sizeof(read_data));
  std::memset(read_buf, 0, sizeof(read_buf));
  bool called = false;
  async_read(s, buffers,
      std::bind(async_read_handler,
        std::placeholders::_1, std::placeholders::_2,
        sizeof(read_data), &called));
  ioc.restart();
  ioc.run();
  ASIO_CHECK(called);
  ASIO_CHECK(s.check_buffers(buffers, sizeof(read_data)));

  s.reset(read_data, sizeof(read_data));
  s.next_read_length(1);
  std::memset(read_buf, 0, sizeof(read_buf));
  called = false;
  async_read(s, buffers,
      std::bind(async_read_handler,
        std::placeholders::_1, std::placeholders::_2,
        sizeof(read_data), &called));
  ioc.restart();
  ioc.run();
  ASIO_CHECK(called);
  ASIO_CHECK(s.check_buffers(buffers, sizeof(read_data)));

  s.reset(read_data, sizeof(read_data));
  s.next_read_length(10);
  std::memset(read_buf, 0, sizeof(read_buf));
  called = false;
  async_read(s, buffers,
      std::bind(async_read_handler,
        std::placeholders::_1, std::placeholders::_2,
        sizeof(read_data), &called));
  ioc.restart();
  ioc.run();
  ASIO_CHECK(called);
  ASIO_CHECK(s.check_buffers(buffers, sizeof(read_data)));
}

// asio completes a composed read with error::eof when the stream ends before
// the buffer is full. This case checks the same behaviour.
void test_async_read_eof()
{
  io_context ioc;
  test_stream s(ioc);
  char read_buf[16];
  mutable_buffer buffers = buffer(read_buf, sizeof(read_buf));

  s.reset(read_data, 5); // shorter than the buffer

  error_code result;
  std::size_t transferred = 0;
  bool called = false;

  async_read(s, buffers, [&](error_code ec, std::size_t n) {
    called = true;
    result = ec;
    transferred = n;
  });

  ioc.restart();
  ioc.run();

  ASIO_CHECK(called);
  ASIO_CHECK(result == eof);
  ASIO_CHECK(transferred == 5);
}

ASIO_TEST_SUITE
(
  "read",
  ASIO_TEST_CASE(test_3_arg_mutable_buffer_async_read)
  ASIO_TEST_CASE(test_async_read_eof)
)
