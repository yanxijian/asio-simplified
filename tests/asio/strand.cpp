//
// strand.cpp
// ~~~~~~~~~~
//
// Adapted from asio/src/tests/unit/strand.cpp
// (Copyright (c) 2003-2026 Christopher M. Kohlhoff,
//  distributed under the Boost Software License 1.0).
//
// Adaptations:
//   * strand<io_context::executor_type> + make_strand(ioc) -> sasio::strand
//   * asio::post(strand, h) / asio::dispatch(strand, h) -> strand::post/dispatch
//   * OMITTED: strand_conversion_test, strand_query_test and
//     strand_execute_test; they need executor conversion, query/prefer and
//     execute(), which sasio does not have.
//   * OMITTED: the "sleep increments" block of strand_test; it relies on
//     synchronous steady_timer::wait().
//   * ADDED: strand_multithread_test (serialisation over two run() threads)
//     and strand_order_test; asio's equivalent coverage lives in that omitted
//     block, so these two cases carry it over using the same idea.
//
#include "sasio/io_context.hpp"
#include "sasio/strand.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>
#include "unit_test.hpp"

using namespace sasio;
namespace bindns = std;

void increment(int* count)
{
  ++(*count);
}

void increment_without_lock(strand* s, int* count)
{
  ASIO_CHECK(!s->running_in_this_thread());

  int original_count = *count;

  s->dispatch(bindns::bind(increment, count));

  // No other functions are currently executing through the locking dispatcher,
  // so the previous call to dispatch should have successfully nested.
  ASIO_CHECK(*count == original_count + 1);
}

void increment_with_lock(strand* s, int* count)
{
  ASIO_CHECK(s->running_in_this_thread());

  int original_count = *count;

  s->dispatch(bindns::bind(increment, count));

  // The current function already holds the strand's lock, so the
  // previous call to dispatch should have successfully nested.
  ASIO_CHECK(*count == original_count + 1);
}

void throw_exception()
{
  throw 1;
}

void strand_test()
{
  io_context ioc;
  strand s(ioc);
  int count = 0;

  ASIO_CHECK(!s.running_in_this_thread());

  ioc.post(bindns::bind(increment_without_lock, &s, &count));

  // No handlers can be called until run() is called.
  ASIO_CHECK(count == 0);

  ioc.run();

  // The run() call will not return until all work has finished.
  ASIO_CHECK(count == 1);

  count = 0;
  ioc.restart();
  s.post(bindns::bind(increment_with_lock, &s, &count));

  // No handlers can be called until run() is called.
  ASIO_CHECK(count == 0);

  ioc.run();

  ASIO_CHECK(count == 1);

  // Exceptions must not leave the strand locked: the remaining handlers still
  // run, and each exception propagates out of run().
  count = 0;
  int exception_count = 0;
  ioc.restart();
  s.post(throw_exception);
  s.post(bindns::bind(increment, &count));
  s.post(bindns::bind(increment, &count));
  s.post(throw_exception);
  s.post(bindns::bind(increment, &count));

  ASIO_CHECK(count == 0);
  ASIO_CHECK(exception_count == 0);

  for (;;)
  {
    try
    {
      ioc.run();
      break;
    }
    catch (int)
    {
      ++exception_count;
    }
  }

  ASIO_CHECK(count == 3);
  ASIO_CHECK(exception_count == 2);
}

void strand_order_test()
{
  io_context ioc;
  strand s(ioc);
  std::vector<int> order;

  for (int i = 0; i < 10; ++i)
    s.post([&order, i]() { order.push_back(i); });

  ioc.run();

  ASIO_CHECK(order.size() == 10);
  for (int i = 0; i < 10; ++i)
    ASIO_CHECK(order[static_cast<std::size_t>(i)] == i);
}

void strand_multithread_test()
{
  io_context ioc;
  strand s(ioc);

  const int total = 100;
  std::atomic<int> active{0};
  std::atomic<int> max_active{0};
  std::atomic<int> completed{0};

  for (int i = 0; i < total; ++i)
  {
    s.post([&]() {
      const int now = active.fetch_add(1) + 1;

      int previous = max_active.load();
      while (now > previous
          && !max_active.compare_exchange_weak(previous, now))
      {
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(1));

      active.fetch_sub(1);
      completed.fetch_add(1);
    });
  }

  std::thread first([&ioc]() { ioc.run(); });
  std::thread second([&ioc]() { ioc.run(); });

  first.join();
  second.join();

  ASIO_CHECK(completed.load() == total);
  ASIO_CHECK(max_active.load() == 1);
}

ASIO_TEST_SUITE
(
  "strand",
  ASIO_TEST_CASE(strand_test)
  ASIO_TEST_CASE(strand_order_test)
  ASIO_TEST_CASE(strand_multithread_test)
)
