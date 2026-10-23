//
// awaitable.hpp
// ~~~~~~~~~~~~~
//
// A minimal C++20 coroutine return type, modelled on asio::awaitable:
//
//   awaitable<int> compute() { co_return 42; }
//   awaitable<void> wait(steady_timer& t) { co_await async_wait(t); }
//
// asio's awaitable carries an executor and works with every completion token
// through async_result. Here it is a plain coroutine task: no executor, and
// the asynchronous adapters live in co_spawn.hpp.
//
// asio counterpart: asio/awaitable.hpp, asio/detail/awaitable_frame.hpp,
//                   asio/impl/awaitable.hpp
//
#ifndef SASIO_AWAITABLE_HPP
#define SASIO_AWAITABLE_HPP

#include "sasio/error.hpp"

#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sasio {

template <typename T>
class awaitable
{
public:
  struct promise_type
  {
    std::optional<T> value;
    std::exception_ptr error;
    std::function<void(std::exception_ptr, T)> completion;
    std::coroutine_handle<> continuation;
    bool top_level = false;

    awaitable get_return_object()
    {
      return awaitable(
          std::coroutine_handle<promise_type>::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter
    {
      promise_type* self;

      // Top-level coroutines (started by co_spawn) do not suspend at the end:
      // the frame is destroyed automatically and ~promise_type runs the
      // completion handler. Awaited coroutines do suspend and hand control to
      // their awaiting coroutine via symmetric transfer.
      bool await_ready() const noexcept { return self->top_level; }

      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<promise_type> handle) noexcept
      {
        std::coroutine_handle<> next = handle.promise().continuation;
        return next ? next : std::noop_coroutine();
      }

      void await_resume() noexcept {}
    };

    final_awaiter final_suspend() noexcept { return {this}; }

    void return_value(T result) { value = std::move(result); }
    void unhandled_exception() { error = std::current_exception(); }

    ~promise_type()
    {
      // A top-level coroutine (started by co_spawn) lets its frame be destroyed
      // automatically at completion; this is where its completion handler runs.
      if (completion)
      {
        try
        {
          completion(error, value ? std::move(*value) : T{});
        }
        catch (...)
        {
        }
      }
    }
  };

  using handle_type = std::coroutine_handle<promise_type>;

  explicit awaitable(handle_type handle) : handle_(handle) {}

  awaitable(awaitable&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
  {
  }

  awaitable& operator=(awaitable&& other) noexcept
  {
    if (this != &other)
    {
      if (handle_)
        handle_.destroy();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  awaitable(const awaitable&) = delete;
  awaitable& operator=(const awaitable&) = delete;

  ~awaitable()
  {
    if (handle_)
      handle_.destroy();
  }

  handle_type release() { return std::exchange(handle_, nullptr); }

  bool valid() const { return handle_ != nullptr; }

  struct awaiter
  {
    handle_type handle;

    bool await_ready() const noexcept
    {
      return !handle || handle.done();
    }

    void await_suspend(std::coroutine_handle<> awaiting)
    {
      handle.promise().continuation = awaiting;
      handle.resume();
    }

    T await_resume()
    {
      if (handle.promise().error)
        std::rethrow_exception(handle.promise().error);
      return std::move(*handle.promise().value);
    }
  };

  // Only rvalue awaitables can be awaited: the task owns its coroutine frame,
  // so awaiting a named awaitable requires co_await std::move(task). asio's
  // awaitable has the same move-only ownership rule.
  awaiter operator co_await() && { return awaiter{handle_}; }

private:
  handle_type handle_ = nullptr;
};

//------------------------------------------------------------------------------
// awaitable<void>

template <>
class awaitable<void>
{
public:
  struct promise_type
  {
    std::exception_ptr error;
    std::function<void(std::exception_ptr)> completion;
    std::coroutine_handle<> continuation;
    bool top_level = false;

    awaitable get_return_object()
    {
      return awaitable(
          std::coroutine_handle<promise_type>::from_promise(*this));
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter
    {
      promise_type* self;

      bool await_ready() const noexcept { return self->top_level; }

      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<promise_type> handle) noexcept
      {
        std::coroutine_handle<> next = handle.promise().continuation;
        return next ? next : std::noop_coroutine();
      }

      void await_resume() noexcept {}
    };

    final_awaiter final_suspend() noexcept { return {this}; }

    void return_void() {}
    void unhandled_exception() { error = std::current_exception(); }

    ~promise_type()
    {
      if (completion)
      {
        try
        {
          completion(error);
        }
        catch (...)
        {
        }
      }
    }
  };

  using handle_type = std::coroutine_handle<promise_type>;

  explicit awaitable(handle_type handle) : handle_(handle) {}

  awaitable(awaitable&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
  {
  }

  awaitable& operator=(awaitable&& other) noexcept
  {
    if (this != &other)
    {
      if (handle_)
        handle_.destroy();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  awaitable(const awaitable&) = delete;
  awaitable& operator=(const awaitable&) = delete;

  ~awaitable()
  {
    if (handle_)
      handle_.destroy();
  }

  handle_type release() { return std::exchange(handle_, nullptr); }

  bool valid() const { return handle_ != nullptr; }

  struct awaiter
  {
    handle_type handle;

    bool await_ready() const noexcept
    {
      return !handle || handle.done();
    }

    void await_suspend(std::coroutine_handle<> awaiting)
    {
      handle.promise().continuation = awaiting;
      handle.resume();
    }

    void await_resume()
    {
      if (handle.promise().error)
        std::rethrow_exception(handle.promise().error);
    }
  };

  // See the primary template: awaiting is rvalue-only because it transfers
  // ownership of the coroutine frame.
  awaiter operator co_await() && { return awaiter{handle_}; }

private:
  handle_type handle_ = nullptr;
};

//------------------------------------------------------------------------------
// Bridges a callback-based operation into an awaitable.

template <typename T>
class async_operation
{
public:
  using callback_type = std::function<void(error_code, T)>;

  explicit async_operation(std::function<void(callback_type)> start)
    : start_(std::move(start))
  {
  }

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> awaiting)
  {
    awaiting_ = awaiting;

    auto start = std::move(start_);
    start([this](error_code ec, T value) {
      error_ = ec;
      value_ = std::move(value);
      awaiting_.resume();
    });
  }

  T await_resume()
  {
    if (error_)
      throw std::system_error(error_);
    return std::move(value_);
  }

private:
  std::function<void(callback_type)> start_;
  std::coroutine_handle<> awaiting_;
  error_code error_;
  T value_{};
};

template <>
class async_operation<void>
{
public:
  using callback_type = std::function<void(error_code)>;

  explicit async_operation(std::function<void(callback_type)> start)
    : start_(std::move(start))
  {
  }

  bool await_ready() const noexcept { return false; }

  void await_suspend(std::coroutine_handle<> awaiting)
  {
    awaiting_ = awaiting;

    auto start = std::move(start_);
    start([this](error_code ec) {
      error_ = ec;
      awaiting_.resume();
    });
  }

  void await_resume()
  {
    if (error_)
      throw std::system_error(error_);
  }

private:
  std::function<void(callback_type)> start_;
  std::coroutine_handle<> awaiting_;
  error_code error_;
};

} // namespace sasio

#endif // SASIO_AWAITABLE_HPP
