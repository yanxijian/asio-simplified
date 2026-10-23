//
// co_spawn.hpp
// ~~~~~~~~~~~~
//
// Starts a coroutine on an io_context, and provides awaitable adapters for the
// callback-based operations:
//
//   awaitable<void> session(tcp::socket& s)
//   {
//     char data[1024];
//     std::size_t n = co_await async_read_some(s, buffer(data));
//     co_await async_write_some(s, buffer(data, n));
//   }
//
//   co_spawn(ctx, session(socket),
//       [](std::exception_ptr e) { /* coroutine finished */ });
//
// asio's co_spawn() accepts any completion token and its operations expose
// use_awaitable; here the adapters are free functions returning awaitable, so
// no completion-token dispatch is needed.
//
// asio counterpart: asio/co_spawn.hpp, asio/use_awaitable.hpp,
//                   asio/impl/co_spawn.hpp
//
#ifndef SASIO_CO_SPAWN_HPP
#define SASIO_CO_SPAWN_HPP

#include "sasio/awaitable.hpp"
#include "sasio/buffer.hpp"
#include "sasio/ip/tcp.hpp"
#include "sasio/steady_timer.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <utility>

namespace sasio {

class io_context;

template <typename T, typename Handler>
void co_spawn(io_context&, awaitable<T> task, Handler&& handler)
{
  auto handle = task.release();
  handle.promise().top_level = true;
  handle.promise().completion =
      std::function<void(std::exception_ptr, T)>(std::forward<Handler>(handler));
  handle.resume();
}

template <typename Handler>
void co_spawn(io_context&, awaitable<void> task, Handler&& handler)
{
  auto handle = task.release();
  handle.promise().top_level = true;
  handle.promise().completion =
      std::function<void(std::exception_ptr)>(std::forward<Handler>(handler));
  handle.resume();
}

//------------------------------------------------------------------------------
// Awaitable adapters for the callback-based operations.

inline awaitable<void> async_wait(steady_timer& timer)
{
  co_await async_operation<void>(
      [&timer](std::function<void(error_code)> handler) {
        timer.async_wait(std::move(handler));
      });
}

inline awaitable<void> async_connect(ip::tcp::socket& socket,
    const ip::tcp::endpoint& ep)
{
  co_await async_operation<void>(
      [&socket, ep](std::function<void(error_code)> handler) {
        socket.async_connect(ep, std::move(handler));
      });
}

inline awaitable<void> async_accept(ip::tcp::acceptor& acceptor,
    ip::tcp::socket& peer)
{
  co_await async_operation<void>(
      [&acceptor, &peer](std::function<void(error_code)> handler) {
        acceptor.async_accept(peer, std::move(handler));
      });
}

inline awaitable<std::size_t> async_read_some(ip::tcp::socket& socket,
    mutable_buffer buffer)
{
  co_return co_await async_operation<std::size_t>(
      [&socket, buffer](
          std::function<void(error_code, std::size_t)> handler) {
        socket.async_read_some(buffer, std::move(handler));
      });
}

inline awaitable<std::size_t> async_write_some(ip::tcp::socket& socket,
    const_buffer buffer)
{
  co_return co_await async_operation<std::size_t>(
      [&socket, buffer](
          std::function<void(error_code, std::size_t)> handler) {
        socket.async_write_some(buffer, std::move(handler));
      });
}

} // namespace sasio

#endif // SASIO_CO_SPAWN_HPP
