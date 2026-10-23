#include "sasio/io_context.hpp"

#include "sasio/scheduler.hpp"

#include <utility>

namespace sasio {

io_context::io_context()
  : scheduler_(use_service<scheduler>())
{
}

io_context::~io_context() = default;

void io_context::post(std::function<void()> handler)
{
  scheduler_.post(std::move(handler));
}

void io_context::dispatch(std::function<void()> handler)
{
  scheduler_.dispatch(std::move(handler));
}

std::size_t io_context::run() { return scheduler_.run(); }
std::size_t io_context::run_one() { return scheduler_.run_one(); }
std::size_t io_context::poll() { return scheduler_.poll(); }
std::size_t io_context::poll_one() { return scheduler_.poll_one(); }

void io_context::stop() { scheduler_.stop(); }
bool io_context::stopped() const { return scheduler_.stopped(); }
void io_context::restart() { scheduler_.restart(); }

io_context::work::work(io_context& ctx)
  : scheduler_(&ctx.use_service<scheduler>())
{
  scheduler_->on_work_started();
}

io_context::work::work(work&& other) noexcept
  : scheduler_(other.scheduler_)
{
  other.scheduler_ = nullptr;
}

io_context::work& io_context::work::operator=(work&& other) noexcept
{
  if (this != &other)
  {
    reset();
    scheduler_ = other.scheduler_;
    other.scheduler_ = nullptr;
  }
  return *this;
}

io_context::work::~work()
{
  reset();
}

void io_context::work::reset()
{
  if (scheduler_)
  {
    scheduler_->on_work_finished();
    scheduler_ = nullptr;
  }
}

} // namespace sasio
