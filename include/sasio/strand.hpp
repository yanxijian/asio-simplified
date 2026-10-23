//
// strand.hpp
// ~~~~~~~~~~
//
// A strand serialises handlers: two handlers submitted through the same
// strand never run at the same time, even if run() is called on several
// threads. Handlers posted through a strand also keep their order.
//
// asio's strand is an executor adaptor (strand<Executor>) with bind_executor()
// and query/prefer support. Here it is a small object bound to one io_context,
// because sasio has no executor type to adapt.
//
// asio counterpart: asio/strand.hpp, asio/detail/strand_service.hpp
//
#ifndef SASIO_STRAND_HPP
#define SASIO_STRAND_HPP

#include <functional>
#include <memory>
#include <utility>

namespace sasio {

class io_context;

class strand
{
public:
  explicit strand(io_context& ctx);

  // Copies share the same serialisation state, exactly like asio: copying a
  // strand does not create a second, independent strand.
  strand(const strand&) noexcept = default;
  strand& operator=(const strand&) noexcept = default;

  io_context& context() const;

  template <typename Handler>
  void post(Handler&& handler)
  {
    post_impl(std::function<void()>(std::forward<Handler>(handler)));
  }

  template <typename Handler>
  void dispatch(Handler&& handler)
  {
    dispatch_impl(std::function<void()>(std::forward<Handler>(handler)));
  }

  bool running_in_this_thread() const;

private:
  struct impl;

  void post_impl(std::function<void()> handler);
  void dispatch_impl(std::function<void()> handler);

  io_context& context_;
  std::shared_ptr<impl> impl_;
};

} // namespace sasio

#endif // SASIO_STRAND_HPP
