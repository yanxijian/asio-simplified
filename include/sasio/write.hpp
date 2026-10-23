//
// write.hpp
// ~~~~~~~~~
//
// A composed operation: async_write() keeps calling async_write_some() until
// the whole buffer has been written. It is the first "operation made of
// operations" in sasio, and mirrors asio's composed write operation.
//
// Note the ownership trick: the state object is held by a shared_ptr captured
// by each completion handler, so it lives exactly as long as the chain of
// operations it drives. asio achieves the same with a composed_op object plus
// the associated allocator.
//
// asio counterpart: asio/write.hpp, asio/detail/write_op.hpp
//
#ifndef SASIO_WRITE_HPP
#define SASIO_WRITE_HPP

#include "sasio/buffer.hpp"
#include "sasio/error.hpp"

#include <functional>
#include <memory>
#include <utility>

namespace sasio {
namespace detail {

template <typename Stream>
class write_op : public std::enable_shared_from_this<write_op<Stream>>
{
public:
  write_op(Stream& stream, const_buffer buffer,
      std::function<void(error_code, std::size_t)> handler)
    : stream_(stream), buffer_(buffer), handler_(std::move(handler))
  {
  }

  void start() { do_write(); }

private:
  void do_write()
  {
    if (transferred_ == buffer_.size())
    {
      complete(error_code());
      return;
    }

    auto self = this->shared_from_this();
    const char* data = static_cast<const char*>(buffer_.data()) + transferred_;

    stream_.async_write_some(const_buffer(data, buffer_.size() - transferred_),
        [self](error_code ec, std::size_t bytes) {
          self->transferred_ += bytes;

          if (ec)
          {
            self->complete(ec);
            return;
          }

          // A zero-byte write would spin; treat it as completion.
          if (bytes == 0)
          {
            self->complete(error_code());
            return;
          }

          self->do_write();
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
  const_buffer buffer_;
  std::function<void(error_code, std::size_t)> handler_;
  std::size_t transferred_ = 0;
};

} // namespace detail

template <typename Stream, typename Handler>
void async_write(Stream& stream, const_buffer buffer, Handler&& handler)
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

  auto op = std::make_shared<detail::write_op<Stream>>(
      stream, buffer, std::move(erased));
  op->start();
}

} // namespace sasio

#endif // SASIO_WRITE_HPP
