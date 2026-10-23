#include <sasio/io_context.hpp>

#include <cstdio>
#include <functional>

// Chapter 1, example 1: the smallest possible io_context program.
//
// It shows the single most important rule of Asio's model: a handler is never
// called by the code that posts it. post() only puts it in the scheduler's
// queue; run() is what drains the queue.
//
// asio equivalent: src/examples/cpp11/tutorial/timer1/timer.cpp (the post part)

int main()
{
  sasio::io_context ctx;
  int step = 0;

  std::function<void()> tick = [&]() {
    std::printf("handler %d\n", ++step);
    if (step < 3)
      ctx.post(tick);
  };

  std::printf("main: posting three handlers\n");
  ctx.post(tick);

  const std::size_t ran = ctx.run();
  std::printf("run() returned, %zu handler(s) ran\n", ran);
  return 0;
}
