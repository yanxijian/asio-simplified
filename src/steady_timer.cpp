#include "sasio/steady_timer.hpp"

#include "sasio/io_context.hpp"
#include "sasio/scheduler.hpp"

#include <utility>

namespace sasio {

steady_timer::steady_timer(io_context& ctx)
  : timers_(ctx.use_service<timer_queue>()),
    scheduler_(ctx.use_service<scheduler>())
{
}

steady_timer::~steady_timer()
{
  cancel();
}

std::size_t steady_timer::expires_at(time_point t)
{
  const std::size_t cancelled = cancel();
  expiry_ = t;
  return cancelled;
}

void steady_timer::async_wait(std::function<void(error_code)> handler)
{
  // Only one wait at a time (see the header): a second async_wait() cancels
  // the first. asio supports several waits; the service keeps a list of them.
  cancel();

  auto entry = std::make_shared<timer_queue::entry>();
  entry->when = expiry_;
  entry->handler = std::move(handler);

  pending_ = std::move(entry);

  // A thread may already be blocked in WSAPoll() with an infinite timeout
  // that was computed before this timer existed. If this is the new earliest
  // timer, wake it so it recomputes. asio reaches the same effect from
  // select_reactor::schedule_timer(): if (earliest) interrupter_.interrupt();
  if (timers_.add(pending_))
    scheduler_.wake_waiters();
}

std::size_t steady_timer::cancel()
{
  std::shared_ptr<timer_queue::entry> entry = std::move(pending_);
  pending_.reset();

  if (!entry)
    return 0;

  // If the entry already expired, remove() says "not in the heap" and the
  // completion has already been queued by the scheduler.
  if (!timers_.remove(entry))
    return 0;

  std::function<void(error_code)> handler = std::move(entry->handler);
  if (!handler)
    return 1;

  // Never invoke the handler from inside cancel(): post it, exactly like asio.
  scheduler_.post([handler = std::move(handler)]() mutable {
    handler(operation_aborted);
  });

  return 1;
}

} // namespace sasio
