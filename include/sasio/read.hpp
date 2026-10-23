//
// read.hpp
// ~~~~~~~~
//
// The read counterpart of write.hpp: keep calling async_read_some() until the
// buffer is full, the stream ends, or an error occurs.
//
// Ending early is reported as sasio::eof, matching asio's behaviour of
// completing a composed read with asio::error::eof when the peer closes before
// the requested number of bytes arrived.
//
// asio counterpart: asio/read.hpp, asio/detail/read_op.hpp
//
#ifndef SASIO_READ_HPP
#define SASIO_READ_HPP

#include "sasio/buffer.hpp"
#include "sasio/error.hpp"

#include <functional>
#include <memory>
#include <utility>

namespace sasio {
namespace detail {

template <typename Stream>
class read_op : public std::enable_shared_from_this<read_op<Stream>>
{
public:
  read_op(Stream& stream, mutable_buffer buffer,
      std::function<void(error_code, std::size_t)> handler)
    : stream_(stream), buffer_(buffer), handler_(std::move(handler))
  {
  }

  void start() { do_read(); }

private:
  void do_read()
  {
    if (transferred_ == buffer_.size())
    {
      complete(error_code());
      return;
    }

    auto self = this->shared_from_this();
    char* data = static_cast<char*>(buffer_.data()) + transferred_;

    stream_.async_read_some(mutable_buffer(data, buffer_.size() - transferred_),
        [self](error_code ec, std::size_t bytes) {
          self->transferred_ += bytes;

          if (ec)
          {
            self->complete(ec);
            return;
          }

          // The stream ended before the buffer was full.
          if (bytes == 0)
          {
            self->complete(eof);
            return;
          }

          self->do_read();
        });
  }

  void complete(error_code ec)
  {
    std::function<void(error_code, std::size_t)> handler = std::move(handler_);
    handler_ = nullptr;
    if (handler)
      handler(ec, transferred_);
  }

  Stream& stream_;
  mutable_buffer buffer_;
  std::function<void(error_code, std::size_t)> handler_;
  std::size_t transferred_ = 0;
};

} // namespace detail

template <typename Stream, typename Handler>
void async_read(Stream& stream, mutable_buffer buffer, Handler&& handler)
{
  std::function<void(error_code, std::size_t)> erased(
      std::forward<Handler>(handler));

  if (buffer.size() == 0)
  {
    stream.context().post([erased]() mutable {
      erased(error_code(), 0);
    });
    return;
  }

  auto op = std::make_shared<detail::read_op<Stream>>(
      stream, buffer, std::move(erased));
  op->start();
}

} // namespace sasio

#endif // SASIO_READ_HPP
