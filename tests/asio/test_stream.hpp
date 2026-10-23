//
// test_stream.hpp
// ~~~~~~~~~~~~~~~
//
// Adapted from the test_stream used by asio/src/tests/unit/read.cpp and
// write.cpp (Copyright (c) 2003-2026 Christopher M. Kohlhoff, Boost Software
// License 1.0).
//
// It is not a socket: async_read_some/async_write_some complete immediately
// with at most next_read_length()/next_write_length() bytes, which is enough
// to exercise partial transfers in composed operations.
//
#ifndef SASIO_TEST_STREAM_HPP
#define SASIO_TEST_STREAM_HPP

#include "sasio/buffer.hpp"
#include "sasio/error.hpp"
#include "sasio/io_context.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

#include "unit_test.hpp"

class test_stream
{
public:
  explicit test_stream(sasio::io_context& ctx)
    : ctx_(ctx),
      length_(max_length),
      position_(0),
      next_write_length_(max_length),
      next_read_length_(max_length)
  {
    std::memset(data_, 0, max_length);
  }

  sasio::io_context& context() const { return ctx_; }

  // For write tests: fill with zeroes and start from the beginning.
  void reset()
  {
    std::memset(data_, 0, max_length);
    length_ = max_length;
    position_ = 0;
    next_write_length_ = max_length;
    next_read_length_ = max_length;
  }

  // For read tests: seed the stream with the data to be read.
  void reset(const char* data, std::size_t length)
  {
    ASIO_CHECK(length <= max_length);
    std::memset(data_, 0, max_length);
    std::memcpy(data_, data, length);
    length_ = length;
    position_ = 0;
    next_write_length_ = length;
    next_read_length_ = length;
  }

  void next_write_length(std::size_t length) { next_write_length_ = length; }
  void next_read_length(std::size_t length) { next_read_length_ = length; }

  template <typename Handler>
  void async_write_some(sasio::const_buffer buffer, Handler&& handler)
  {
    const std::size_t bytes = (std::min)({
        buffer.size(), next_write_length_, max_length - position_});

    std::memcpy(data_ + position_, buffer.data(), bytes);
    position_ += bytes;

    auto erased = std::function<void(sasio::error_code, std::size_t)>(
        std::forward<Handler>(handler));

    ctx_.post([erased, bytes]() mutable {
      erased(sasio::error_code(), bytes);
    });
  }

  template <typename Handler>
  void async_read_some(sasio::mutable_buffer buffer, Handler&& handler)
  {
    const std::size_t bytes = (std::min)({
        buffer.size(), next_read_length_, length_ - position_});

    std::memcpy(buffer.data(), data_ + position_, bytes);
    position_ += bytes;

    auto erased = std::function<void(sasio::error_code, std::size_t)>(
        std::forward<Handler>(handler));

    ctx_.post([erased, bytes]() mutable {
      erased(sasio::error_code(), bytes);
    });
  }

  bool check_buffers(sasio::const_buffer buffer, std::size_t length) const
  {
    if (length != position_)
      return false;
    return std::memcmp(data_, buffer.data(), length) == 0;
  }

private:
  enum { max_length = 8192 };

  sasio::io_context& ctx_;
  char data_[max_length];
  std::size_t length_;
  std::size_t position_;
  std::size_t next_write_length_;
  std::size_t next_read_length_;
};

#endif // SASIO_TEST_STREAM_HPP
