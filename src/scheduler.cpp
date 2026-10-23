#include "sasio/scheduler.hpp"

#include "sasio/reactor.hpp"
#include "sasio/timer_queue.hpp"

#include <memory>
#include <utility>

namespace sasio {
namespace {

// Remembers, per thread, which scheduler is currently running one of its
// handlers. dispatch() uses it to decide between "run inline" and "post".
// asio makes the same decision by comparing executors (can_dispatch()), which
// is more general but needs the whole executor machinery.
thread_local scheduler* current_scheduler = nullptr;

class handler_scope
{
public:
  explicit handler_scope(scheduler* s) : previous_(current_scheduler)
  {
    current_scheduler = s;
  }

  ~handler_scope() { current_scheduler = previous_; }

private:
  scheduler* previous_;
};

// Releases the work unit of a queued handler even if the handler throws. This
// mirrors asio's work_cleanup, which is what keeps a throwing handler from
// leaking a unit of outstanding work.
class handler_work
{
public:
  explicit handler_work(scheduler* s) : scheduler_(s) {}
  ~handler_work() { scheduler_->on_work_finished(); }

private:
  scheduler* scheduler_;
};

} // namespace

scheduler::scheduler(execution_context& ctx)
  : reactor_(ctx.use_service<reactor>()),
    timers_(ctx.use_service<timer_queue>())
{
}

void scheduler::shutdown()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopped_.store(true, std::memory_order_release);
    queue_.clear();
  }
  reactor_.interrupt();
}

void scheduler::post(std::function<void()> handler)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // The queued handler owns one unit of work until it has run. This is the
    // simplified form of asio's operation bookkeeping (do_post_immediate_
    // completion calls work_started() for the same reason).
    ++work_count_;
    queue_.push_back(std::move(handler));
  }

  // A thread may be blocked in reactor::run_for() right now.
  reactor_.interrupt();
}

void scheduler::dispatch(std::function<void()> handler)
{
  if (current_scheduler == this)
  {
    handler();
    return;
  }

  post(std::move(handler));
}

void scheduler::invoke(std::function<void()> handler)
{
  handler_scope scope(this);
  handler();
}

std::size_t scheduler::run()
{
  // asio: if there is no outstanding work, stop and return 0 immediately.
  if (!has_work())
  {
    stop();
    return 0;
  }
  return run_impl(true);
}

std::size_t scheduler::run_one()
{
  if (!has_work())
  {
    stop();
    return 0;
  }
  return run_one_impl(true) ? 1u : 0u;
}

std::size_t scheduler::poll()
{
  if (!has_work())
  {
    stop();
    return 0;
  }
  return run_impl(false);
}

std::size_t scheduler::poll_one()
{
  if (!has_work())
  {
    stop();
    return 0;
  }
  return run_one_impl(false) ? 1u : 0u;
}

std::size_t scheduler::run_impl(bool block)
{
  std::size_t count = 0;
  while (run_one_impl(block))
    ++count;
  return count;
}

bool scheduler::run_one_impl(bool block)
{
  bool polled = false;

  for (;;)
  {
    // asio's do_run_one() tests the stopped flag before touching the queue:
    // once stopped, no further handler is invoked.
    if (stopped())
      return false;

    // 1. A handler is already queued: run it, then release its work unit.
    std::function<void()> handler;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!queue_.empty())
      {
        handler = std::move(queue_.front());
        queue_.pop_front();
      }
    }

    if (handler)
    {
      handler_work work(this);
      invoke(std::move(handler));
      return true;
    }

    // 2. A timer has expired: complete it. Timers do not hold a counted work
    //    unit here; a pending timer keeps the context alive through
    //    has_work_locked(), and completing the last one stops the scheduler.
    if (std::shared_ptr<timer_queue::entry> expired = timers_.take_expired(clock::now()))
    {
      std::function<void(error_code)> complete = std::move(expired->handler);
      if (complete)
      {
        handler_scope scope(this);
        complete(error_code());
      }
      maybe_stop_if_idle();
      return true;
    }

    // 3. Decide how long we may block.
    bool work;
    clock::duration timeout = clock::duration::zero();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      work = has_work_locked();
      if (work)
      {
        if (!block)
        {
          // poll(): make exactly one non-blocking pass, then report "nothing
          // else is ready".
          if (polled)
            return false;
          timeout = clock::duration::zero();
        }
        else if (timers_.empty())
        {
          timeout = clock::duration::max();
        }
        else
        {
          const clock::time_point now = clock::now();
          const clock::time_point next = timers_.next_expiry();
          timeout = next > now ? next - now : clock::duration::zero();
        }
      }
    }

    if (!work)
    {
      maybe_stop_if_idle();
      return false;
    }

    reactor_.run_for(timeout);
    polled = true;
  }
}

void scheduler::stop()
{
  stopped_.store(true, std::memory_order_release);
  reactor_.interrupt();
}

bool scheduler::stopped() const
{
  return stopped_.load(std::memory_order_acquire);
}

void scheduler::restart()
{
  stopped_.store(false, std::memory_order_release);
}

void scheduler::on_work_started()
{
  std::lock_guard<std::mutex> lock(mutex_);
  ++work_count_;
}

void scheduler::on_work_finished()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    --work_count_;
  }

  // asio's work_finished() calls stop() as soon as the count reaches zero.
  maybe_stop_if_idle();
}

void scheduler::wake_waiters()
{
  // interrupt() takes no scheduler lock, so this is safe from any thread and
  // keeps the documented lock order (scheduler -> timer queue -> reactor).
  reactor_.interrupt();
}

bool scheduler::has_work_locked() const
{
  return work_count_ != 0 || !queue_.empty() || !timers_.empty()
      || !reactor_.idle();
}

bool scheduler::has_work() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return has_work_locked();
}

void scheduler::maybe_stop_if_idle()
{
  bool idle;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    idle = work_count_ == 0 && queue_.empty() && timers_.empty()
        && reactor_.idle();
    if (idle)
      stopped_.store(true, std::memory_order_release);
  }

  if (idle)
    reactor_.interrupt();
}

} // namespace sasio
