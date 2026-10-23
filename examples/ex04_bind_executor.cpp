#include <sasio/io_context.hpp>
#include <sasio/steady_timer.hpp>
#include <sasio/strand.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <thread>
#include <utility>

// This example shows what asio's bind_executor(strand, handler) does, without
// the executor framework: the completion handler is posted to a strand instead
// of running on whichever thread completed the operation.
//
// Run it with two io_context threads. Every timer completion goes through the
// same strand, so max_active stays 1. Remove bind_strand() and the same
// handlers can overlap.

using namespace sasio;

namespace {

std::function<void(error_code)> bind_strand(strand& s,
    std::function<void(error_code)> handler)
{
  return [&s, handler = std::move(handler)](error_code ec) {
    s.post([handler, ec]() mutable {
      handler(ec);
    });
  };
}

} // namespace

int main()
{
  io_context ctx;
  strand serial(ctx);

  std::atomic<int> active{0};
  std::atomic<int> max_active{0};
  std::atomic<int> completed{0};

  const int timer_count = 24;
  std::deque<steady_timer> timers;
  for (int i = 0; i < timer_count; ++i)
  {
    timers.emplace_back(ctx);
    timers.back().expires_after(std::chrono::milliseconds(5 + i % 7));

    timers.back().async_wait(bind_strand(serial, [&](error_code ec) {
      if (ec)
        return;

      const int now = active.fetch_add(1) + 1;

      int previous = max_active.load();
      while (now > previous
          && !max_active.compare_exchange_weak(previous, now))
      {
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      active.fetch_sub(1);
      completed.fetch_add(1);
    }));
  }

  std::thread first([&ctx]() { ctx.run(); });
  std::thread second([&ctx]() { ctx.run(); });
  first.join();
  second.join();

  std::printf("completed=%d max_active=%d (1 means serialised)\n",
      completed.load(), max_active.load());

  return max_active.load() == 1 ? 0 : 1;
}
