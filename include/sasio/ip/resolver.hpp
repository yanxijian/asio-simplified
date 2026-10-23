//
// resolver.hpp
// ~~~~~~~~~~~~
//
// asio's resolver is protocol-templated (basic_resolver<tcp>, <udp>) and its
// async form runs on a hidden thread pool that is part of the library. Here it
// is one class returning IPv4 tcp endpoints, and async_resolve() uses one
// short-lived worker thread per query:
//
//   resolver r(ctx);
//   auto results = r.resolve(resolver::query("localhost", "80"));
//   r.async_resolve(query, [](error_code ec, resolver::results results) { ... });
//
// asio counterpart: asio/ip/basic_resolver.hpp, asio/ip/resolver_service.hpp
//
#ifndef SASIO_IP_RESOLVER_HPP
#define SASIO_IP_RESOLVER_HPP

#include "sasio/error.hpp"
#include "sasio/ip/tcp.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace sasio {

class io_context;
class scheduler;

namespace ip {

class resolver
{
public:
  using results = std::vector<tcp::endpoint>;

  class query
  {
  public:
    query(std::string host, std::string service)
      : host_(std::move(host)), service_(std::move(service)) {}

    const std::string& host_name() const { return host_; }
    const std::string& service_name() const { return service_; }

  private:
    std::string host_;
    std::string service_;
  };

  explicit resolver(io_context& ctx);

  // Throws std::system_error with a resolver error on failure.
  results resolve(const query& q) const;

  template <typename Handler>
  void async_resolve(const query& q, Handler&& handler)
  {
    start_resolve(q, std::function<void(error_code, results)>(
            std::forward<Handler>(handler)));
  }

  // Cancels the most recent async_resolve(): its handler completes with
  // operation_aborted as soon as the worker thread returns.
  void cancel();

private:
  void start_resolve(const query& q,
      std::function<void(error_code, results)> handler);

  io_context& context_;
  scheduler& scheduler_;

  std::mutex mutex_;
  std::shared_ptr<std::atomic<bool>> cancel_flag_;
};

} // namespace ip
} // namespace sasio

#endif // SASIO_IP_RESOLVER_HPP
