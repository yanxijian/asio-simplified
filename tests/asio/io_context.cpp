//
// io_context.cpp
// ~~~~~~~~~~~~~~
//
// Adapted from asio/src/tests/unit/io_context.cpp
// (Copyright (c) 2003-2026 Christopher M. Kohlhoff,
//  distributed under the Boost Software License 1.0).
//
// Adaptations from the original:
//   * asio::post(ioc, h) / asio::dispatch(ioc, h)
//       -> ioc.post(h) / ioc.dispatch(h)   (sasio has no free functions)
//   * executor_work_guard + make_work_guard
//       -> io_context::work
//   * OMITTED: the multi-threaded block that needs steady_timer::wait() and
//     asio::thread; sasio has no synchronous wait in chapter 1.
//   * OMITTED: the "second io_context" block that needs asio::bind_executor;
//     sasio has no executor type yet.
//   * OMITTED: the service / executor-query / executor-execute / allocator
//     suites; sasio has no public executor or allocator support.
//
// Everything that remains - the handler bodies, the assertions and the
// comments - is asio's own test logic.
//
#include "sasio/io_context.hpp"

#include <functional>
#include "unit_test.hpp"

using namespace sasio;
namespace bindns = std;

void increment(int* count)
{
  ++(*count);
}

void decrement_to_zero(io_context* ioc, int* count)
{
  if (*count > 0)
  {
    --(*count);

    int before_value = *count;
    ioc->post(bindns::bind(decrement_to_zero, ioc, count));

    // Handler execution cannot nest, so count value should remain unchanged.
    ASIO_CHECK(*count == before_value);
  }
}

void nested_decrement_to_zero(io_context* ioc, int* count)
{
  if (*count > 0)
  {
    --(*count);

    ioc->dispatch(bindns::bind(nested_decrement_to_zero, ioc, count));

    // Handler execution is nested, so count value should now be zero.
    ASIO_CHECK(*count == 0);
  }
}

void throw_exception()
{
  throw 1;
}

void io_context_test()
{
  io_context ioc;
  int count = 0;

  ioc.post(bindns::bind(increment, &count));

  // No handlers can be called until run() is called.
  ASIO_CHECK(!ioc.stopped());
  ASIO_CHECK(count == 0);

  ioc.run();

  // The run() call will not return until all work has finished.
  ASIO_CHECK(ioc.stopped());
  ASIO_CHECK(count == 1);

  count = 0;
  ioc.restart();
  ioc.post(bindns::bind(increment, &count));
  ioc.post(bindns::bind(increment, &count));
  ioc.post(bindns::bind(increment, &count));
  ioc.post(bindns::bind(increment, &count));
  ioc.post(bindns::bind(increment, &count));

  // No handlers can be called until run() is called.
  ASIO_CHECK(!ioc.stopped());
  ASIO_CHECK(count == 0);

  ioc.run();

  // The run() call will not return until all work has finished.
  ASIO_CHECK(ioc.stopped());
  ASIO_CHECK(count == 5);

  count = 0;
  ioc.restart();
  io_context::work w(ioc);
  ioc.post(bindns::bind(&io_context::stop, &ioc));
  ASIO_CHECK(!ioc.stopped());
  ioc.run();

  // The only operation executed should have been to stop run().
  ASIO_CHECK(ioc.stopped());
  ASIO_CHECK(count == 0);

  ioc.restart();
  ioc.post(bindns::bind(increment, &count));
  w.reset();

  // No handlers can be called until run() is called.
  ASIO_CHECK(!ioc.stopped());
  ASIO_CHECK(count == 0);

  ioc.run();

  // The run() call will not return until all work has finished.
  ASIO_CHECK(ioc.stopped());
  ASIO_CHECK(count == 1);

  count = 10;
  ioc.restart();
  ioc.post(bindns::bind(decrement_to_zero, &ioc, &count));

  // No handlers can be called until run() is called.
  ASIO_CHECK(!ioc.stopped());
  ASIO_CHECK(count == 10);

  ioc.run();

  // The run() call will not return until all work has finished.
  ASIO_CHECK(ioc.stopped());
  ASIO_CHECK(count == 0);

  count = 10;
  ioc.restart();
  ioc.post(bindns::bind(nested_decrement_to_zero, &ioc, &count));

  // No handlers can be called until run() is called.
  ASIO_CHECK(!ioc.stopped());
  ASIO_CHECK(count == 10);

  ioc.run();

  // The run() call will not return until all work has finished.
  ASIO_CHECK(ioc.stopped());
  ASIO_CHECK(count == 0);

  count = 10;
  ioc.restart();
  ioc.dispatch(bindns::bind(nested_decrement_to_zero, &ioc, &count));

  // No handlers can be called until run() is called, even though nested
  // delivery was specifically allowed in the previous call.
  ASIO_CHECK(!ioc.stopped());
  ASIO_CHECK(count == 10);

  ioc.run();

  // The run() call will not return until all work has finished.
  ASIO_CHECK(ioc.stopped());
  ASIO_CHECK(count == 0);

  // OMITTED (original lines 200-232): two threads running run() while
  // synchronous steady_timer::wait() calls drive the handlers. Needs wait(),
  // which chapter 1 does not provide, and asio::thread.

  count = 0;
  int exception_count = 0;
  ioc.restart();
  ioc.post(&throw_exception);
  ioc.post(bindns::bind(increment, &count));
  ioc.post(bindns::bind(increment, &count));
  ioc.post(&throw_exception);
  ioc.post(bindns::bind(increment, &count));

  // No handlers can be called until run() is called.
  ASIO_CHECK(!ioc.stopped());
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

  // The run() calls will not return until all work has finished.
  ASIO_CHECK(ioc.stopped());
  ASIO_CHECK(count == 3);
  ASIO_CHECK(exception_count == 2);
}

ASIO_TEST_SUITE
(
  "io_context",
  ASIO_TEST_CASE(io_context_test)
)
