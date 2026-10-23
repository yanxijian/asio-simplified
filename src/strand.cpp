#include "sasio/strand.hpp"

#include "sasio/io_context.hpp"
#include "sasio/scheduler.hpp"

#include <deque>
#include <mutex>
#include <system_error>
#include <utility>

namespace sasio {
namespace {

// Remembers which strand is currently executing on this thread, so that
// dispatch() can run a handler inline (and running_in_this_thread() can
// answer asio's question). asio derives the same answer from the executor.
thread_local void* current_strand_impl = nullptr;

} // namespace

struct strand::impl : std::enable_shared_from_this<strand::impl>
{
  explicit impl(scheduler& sched) : scheduler(sched) {}

  void post(std::function<void()> handler)
  {
    bool schedule = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      queue.push_back(std::move(handler));
      if (!running)
      {
        running = true;
        schedule = true;
      }
    }

    if (schedule)
      schedule_drain();
  }

  void dispatch(std::function<void()> handler)
  {
    if (current_strand_impl == this)
    {
      // Already inside the strand on this thread: asio runs the handler inline.
      execute(std::move(handler));
      return;
    }

    bool run_now = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!running)
      {
        running = true;
        run_now = true;
      }
      else
      {
        // Another thread owns the strand; queue and let it pick this up.
        queue.push_back(std::move(handler));
      }
    }

    if (!run_now)
      return;

    // We acquired the strand. Run this handler, then any handlers that were
    // queued while it ran, then release the strand. drain() handles its own
    // exceptions (and reschedules the remaining handlers), so only the
    // handler invoked here needs a guard.
    try
    {
      execute(std::move(handler));
    }
    catch (...)
    {
      reschedule_after_exception();
      throw;
    }

    drain();
  }

  bool running_in_this_thread() const
  {
    return current_strand_impl == this;
  }

private:
  void schedule_drain()
  {
    auto self = shared_from_this();
    scheduler.post([self]() { self->drain(); });
  }

  void drain()
  {
    for (;;)
    {
      std::function<void()> handler;
      {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty())
        {
          running = false;
          return;
        }

        handler = std::move(queue.front());
        queue.pop_front();
      }

      try
      {
        execute(std::move(handler));
      }
      catch (...)
      {
        reschedule_after_exception();
        throw;
      }
    }
  }

  void execute(std::function<void()> handler)
  {
    void* previous = current_strand_impl;
    current_strand_impl = this;

    struct restore
    {
      void* previous;
      ~restore() { current_strand_impl = previous; }
    } guard = {previous};

    handler();
  }

  // If a handler throws, release the strand but keep any queued handlers
  // alive: they must still run, which is what asio's strand does.
  void reschedule_after_exception()
  {
    bool more = false;
    {
      std::lock_guard<std::mutex> lock(mutex);
      running = false;
      if (!queue.empty())
      {
        running = true;
        more = true;
      }
    }

    if (more)
      schedule_drain();
  }

  scheduler& scheduler;
  std::mutex mutex;
  std::deque<std::function<void()>> queue;
  bool running = false;
};

strand::strand(io_context& ctx)
  : context_(ctx),
    impl_(std::make_shared<impl>(ctx.use_service<scheduler>()))
{
}

io_context& strand::context() const
{
  return context_;
}

bool strand::running_in_this_thread() const
{
  return impl_->running_in_this_thread();
}

void strand::post_impl(std::function<void()> handler)
{
  impl_->post(std::move(handler));
}

void strand::dispatch_impl(std::function<void()> handler)
{
  impl_->dispatch(std::move(handler));
}

} // namespace sasio
