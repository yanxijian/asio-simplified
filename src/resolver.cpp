#include "sasio/ip/resolver.hpp"

#include "sasio/io_context.hpp"
#include "sasio/scheduler.hpp"

#include <system_error>
#include <thread>
#include <utility>

namespace sasio {
namespace ip {
namespace {

resolver::results do_resolve(const resolver::query& q, error_code& ec)
{
  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* info = nullptr;
  const int result = ::getaddrinfo(q.host_name().c_str(),
      q.service_name().c_str(), &hints, &info);

  if (result != 0)
  {
    ec = resolver_error(result);
    return resolver::results();
  }

  resolver::results results;
  for (addrinfo* entry = info; entry != nullptr; entry = entry->ai_next)
  {
    if (entry->ai_addr == nullptr || entry->ai_addrlen < sizeof(sockaddr_in))
      continue;

    const sockaddr_in* address =
        reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
    if (address->sin_family != AF_INET)
      continue;

    results.push_back(tcp::endpoint(ntohl(address->sin_addr.s_addr),
        ntohs(address->sin_port)));
  }

  ::freeaddrinfo(info);
  ec.clear();
  return results;
}

} // namespace

resolver::resolver(io_context& ctx)
  : context_(ctx),
    scheduler_(ctx.use_service<scheduler>())
{
}

resolver::results resolver::resolve(const query& q) const
{
  error_code ec;
  results resolved = do_resolve(q, ec);
  if (ec)
    throw std::system_error(ec, "resolver::resolve");
  return resolved;
}

void resolver::cancel()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (cancel_flag_)
    cancel_flag_->store(true, std::memory_order_release);
}

void resolver::start_resolve(const query& q,
    std::function<void(error_code, results)> handler)
{
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_flag_ = cancelled;
  }

  scheduler* sched = &scheduler_;
  scheduler_.on_work_started();

  try
  {
    std::thread([sched, q, cancelled, handler = std::move(handler)]() mutable {
      error_code ec;
      results resolved = do_resolve(q, ec);

      // Decide cancellation once, when the lookup has finished. A cancel()
      // that arrives after this point no longer changes the result, the same
      // way asio leaves an already-completed operation untouched.
      const bool aborted = cancelled->load(std::memory_order_acquire);

      sched->post(
          [sched, handler = std::move(handler), ec,
              resolved = std::move(resolved), aborted]() mutable {
            // The worker thread holds a work unit: release it even if the
            // handler throws, exactly like the rest of the library.
            struct release
            {
              scheduler* target;
              ~release() { target->on_work_finished(); }
            } guard = {sched};

            handler(aborted ? operation_aborted : ec, resolved);
          });
    }).detach();
  }
  catch (...)
  {
    scheduler_.on_work_finished();
    throw;
  }
}

} // namespace ip
} // namespace sasio
