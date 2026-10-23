//
// steady_timer.cpp
// ~~~~~~~~~~~~~~~~
//
// Adapted from asio/src/tests/unit/deadline_timer.cpp
// (Copyright (c) 2003-2026 Christopher M. Kohlhoff,
//  distributed under the Boost Software License 1.0).
//
// Adaptations from the original:
//   * asio::deadline_timer + boost::posix_time
//       -> sasio::steady_timer + std::chrono
//   * expires_at/expires_from_now -> expires_after
//   * Pending times scaled down (seconds -> hundreds of milliseconds) so the
//     suite does not take ~15 seconds per run. The structure of every check is
//     unchanged.
//   * OMITTED: the synchronous wait() checks at the top of the original
//     deadline_timer_test (lines 82-125); chapter 1 has no wait().
//   * OMITTED: the two-simultaneous-waits + cancel_one() block (lines 199-222);
//     sasio allows only one pending wait per timer.
//   * OMITTED: deadline_timer_custom_allocation_test, deadline_timer_thread_test,
//     deadline_timer_async_result_test and deadline_timer_move_test; they need
//     allocator hooks, wait(), completion tokens and move semantics.
//
#include "sasio/io_context.hpp"
#include "sasio/steady_timer.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include "unit_test.hpp"

using namespace sasio;
namespace bindns = std;

void increment(int* count)
{
  ++(*count);
}

void decrement_to_zero(steady_timer* t, int* count)
{
  if (*count > 0)
  {
    --(*count);

    int before_value = *count;

    t->expires_after(std::chrono::milliseconds(100));
    t->async_wait(bindns::bind(decrement_to_zero, t, count));

    // Completion cannot nest, so count value should remain unchanged.
    ASIO_CHECK(*count == before_value);
  }
}

void increment_if_not_cancelled(int* count, const error_code& ec)
{
  if (!ec)
    ++(*count);
}

void cancel_timer(steady_timer* t)
{
  std::size_t num_cancelled = t->cancel();
  ASIO_CHECK(num_cancelled == 1);
}

void steady_timer_test()
{
  io_context ioc;
  int count = 0;

  // Original: 5 second wait. Scaled to 300 ms.
  std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();

  steady_timer t3(ioc);
  t3.expires_after(std::chrono::milliseconds(300));
  t3.async_wait(bindns::bind(increment, &count));

  // No completions can be delivered until run() is called.
  ASIO_CHECK(count == 0);

  ioc.run();

  // The run() call will not return until all operations have finished, and
  // this should not be until after the timer's expiry time.
  ASIO_CHECK(count == 1);
  std::chrono::steady_clock::time_point end =
      std::chrono::steady_clock::now();
  ASIO_CHECK(end - start >= std::chrono::milliseconds(250));

  // Original: three chained 1 second waits. Scaled to three 100 ms waits.
  count = 3;
  start = std::chrono::steady_clock::now();

  steady_timer t4(ioc);
  t4.expires_after(std::chrono::milliseconds(100));
  t4.async_wait(bindns::bind(decrement_to_zero, &t4, &count));

  // No completions can be delivered until run() is called.
  ASIO_CHECK(count == 3);

  ioc.restart();
  ioc.run();

  // The run() call will not return until all operations have finished, and
  // this should not be until after the timer's final expiry time.
  ASIO_CHECK(count == 0);
  end = std::chrono::steady_clock::now();
  ASIO_CHECK(end - start >= std::chrono::milliseconds(250));

  // Original: a 10 second timer cancelled from a 1 second timer.
  // Scaled to 1 second and 100 ms.
  count = 0;
  start = std::chrono::steady_clock::now();

  steady_timer t5(ioc);
  t5.expires_after(std::chrono::seconds(1));
  t5.async_wait(bindns::bind(increment_if_not_cancelled, &count,
        std::placeholders::_1));

  steady_timer t6(ioc);
  t6.expires_after(std::chrono::milliseconds(100));
  t6.async_wait(bindns::bind(cancel_timer, &t5));

  // No completions can be delivered until run() is called.
  ASIO_CHECK(count == 0);

  ioc.restart();
  ioc.run();

  // The timer should have been cancelled, so count should not have changed.
  // The total run time should not have been much more than 100 ms (and
  // certainly far less than 1 second).
  ASIO_CHECK(count == 0);
  end = std::chrono::steady_clock::now();
  ASIO_CHECK(end < start + std::chrono::seconds(2));

  // Wait on the timer again without cancelling it. This time the asynchronous
  // wait should run to completion and increment the counter.
  t5.async_wait(bindns::bind(increment_if_not_cancelled, &count,
        std::placeholders::_1));

  ioc.restart();
  ioc.run();

  // The timer should not have been cancelled, so count should have changed.
  // The total time since the timer was created should be more than 1 second.
  ASIO_CHECK(count == 1);
  end = std::chrono::steady_clock::now();
  ASIO_CHECK(end - start >= std::chrono::seconds(1));

  // OMITTED (original lines 199-222): two simultaneous waits on one timer,
  // one cancelled through cancel_one(). sasio supports a single pending wait.
}

void timer_handler(const error_code&)
{
}

void steady_timer_cancel_test()
{
  // Original uses deadline_timer with pos_infin; steady_timer::time_point::max()
  // is the same "never expires" value.
  static io_context ioc;
  struct timer
  {
    steady_timer t;
    timer() : t(ioc) { t.expires_at(steady_timer::time_point::max()); }
  } timers[50];

  timers[2].t.async_wait(&timer_handler);
  timers[41].t.async_wait(&timer_handler);
  for (int i = 10; i < 20; ++i)
    timers[i].t.async_wait(&timer_handler);

  ASIO_CHECK(timers[2].t.cancel() == 1);
  ASIO_CHECK(timers[41].t.cancel() == 1);
  for (int i = 10; i < 20; ++i)
    ASIO_CHECK(timers[i].t.cancel() == 1);
}

// Regression test for a missed wakeup: with the only run() thread already
// blocked in WSAPoll() under an infinite timeout, arming a timer from another
// thread must wake it so the timeout is recomputed. asio does exactly this in
// select_reactor::schedule_timer():
//   if (earliest) interrupter_.interrupt();
// Without that wake-up the handler would only ever run after the stop() below.

void timer_multithread_wakeup_test()
{
  io_context ioc;
  io_context::work guard(ioc);

  std::atomic<bool> fired{false};

  std::thread runner([&ioc]() { ioc.run(); });

  // Let the runner block inside WSAPoll with no timers registered.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  steady_timer timer(ioc);
  timer.expires_after(std::chrono::milliseconds(100));
  timer.async_wait([&fired](error_code ec) {
    if (!ec)
      fired = true;
  });

  // Give the timer ample time to expire (or not, if the wake-up is missing),
  // then stop the runner from this thread. stop() has always woken the
  // poller, so a missing wake-up fails the flag check instead of hanging
  // this test in join().
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  ioc.stop();
  runner.join();

  ASIO_CHECK(fired.load());
}

ASIO_TEST_SUITE
(
  "steady_timer",
  ASIO_TEST_CASE(steady_timer_test)
  ASIO_TEST_CASE(steady_timer_cancel_test)
  ASIO_TEST_CASE(timer_multithread_wakeup_test)
)
