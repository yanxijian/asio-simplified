#include <sasio/io_context.hpp>
#include <sasio/steady_timer.hpp>

#include <chrono>
#include <cstdio>
#include <functional>

// Chapter 1, example 2: a repeating timer.
//
// This is where the reactor shows up in the design: run() blocks in WSAPoll
// until the timer queue says the next timer is due. Nothing spins, and no
// thread is dedicated to timers.
//
// asio equivalent: src/examples/cpp11/tutorial/timer2/timer.cpp

int main()
{
  sasio::io_context ctx;
  sasio::steady_timer timer(ctx);
  int ticks = 0;

  std::function<void(sasio::error_code)> on_tick =
      [&](sasio::error_code ec) {
        if (ec)
        {
          std::printf("wait cancelled: %s\n", ec.message().c_str());
          return;
        }

        std::printf("tick %d at %lld ms\n", ++ticks,
            static_cast<long long>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count()));

        if (ticks < 3)
        {
          timer.expires_after(std::chrono::milliseconds(200));
          timer.async_wait(on_tick);
        }
      };

  timer.expires_after(std::chrono::milliseconds(200));
  timer.async_wait(on_tick);

  ctx.run();
  std::printf("done\n");
  return 0;
}
