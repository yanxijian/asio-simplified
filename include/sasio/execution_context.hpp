//
// execution_context.hpp
// ~~~~~~~~~~~~~~~~~~~~~
//
// This is the design idea that makes io_context small in asio.
//
// An execution_context is a bag of services. A service is any piece of
// machinery that lives for the lifetime of the context: the scheduler, the
// reactor, the timer queue, and later the strand service and socket services.
//
// io_context itself contains almost no logic. It is the "front door" that
// creates and finds services:
//
//   io_context ctx;
//   auto& scheduler = ctx.use_service<scheduler>();      // created on demand
//   auto& reactor   = ctx.use_service<reactor>();
//
// The win: a new feature can be added as a service without touching
// io_context, and a user can add their own service to a context.
//
// asio counterpart:
//   asio/execution_context.hpp
//   asio/detail/service_registry.hpp
//
#ifndef SASIO_EXECUTION_CONTEXT_HPP
#define SASIO_EXECUTION_CONTEXT_HPP

#include <memory>
#include <mutex>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sasio {

class execution_context;

// Base class for every service. Derive from this and the context can own it.
class service
{
public:
  virtual ~service() = default;

  // Called when the owning context is being destroyed, in reverse order of
  // creation. Services that hold OS resources or pending handlers release
  // them here.
  virtual void shutdown() {}
};

class execution_context
{
public:
  execution_context() = default;

  execution_context(const execution_context&) = delete;
  execution_context& operator=(const execution_context&) = delete;

  virtual ~execution_context();

  // Returns the service of type T, creating it on first use. Services are
  // constructed with the context as their first argument, which is how
  // asio's service_registry creates them too.
  template <typename T, typename... Args>
  T& use_service(Args&&... args)
  {
    static_assert(std::is_base_of<service, T>::value,
        "a sasio service must derive from sasio::service");

    // Recursive because a service constructor may create other services:
    // scheduler -> reactor, scheduler -> timer_queue.
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    const std::type_index key(typeid(T));
    const auto existing = services_.find(key);
    if (existing != services_.end())
      return static_cast<T&>(*existing->second);

    std::unique_ptr<T> owned(new T(*this, std::forward<Args>(args)...));
    T& created = *owned;
    services_.emplace(key, std::move(owned));
    order_.push_back(&created);
    return created;
  }

protected:
  // Shuts down and destroys all services in reverse order of creation.
  void shutdown_services();

private:
  std::recursive_mutex mutex_;
  std::unordered_map<std::type_index, std::unique_ptr<service>> services_;
  std::vector<service*> order_;
};

} // namespace sasio

#endif // SASIO_EXECUTION_CONTEXT_HPP
