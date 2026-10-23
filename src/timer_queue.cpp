#include "sasio/timer_queue.hpp"

namespace sasio {

timer_queue::timer_queue(execution_context&)
{
}

void timer_queue::shutdown()
{
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& entry : heap_)
    entry->heap_index = entry::npos;
  heap_.clear();
}

bool timer_queue::add(std::shared_ptr<entry> e)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const bool earliest = heap_.empty() || e->when < heap_.front()->when;
  e->heap_index = heap_.size();
  heap_.push_back(std::move(e));
  sift_up(heap_.size() - 1);
  return earliest;
}

bool timer_queue::remove(const std::shared_ptr<entry>& e)
{
  std::lock_guard<std::mutex> lock(mutex_);

  // The index is only a hint: an entry that already expired (or was removed)
  // must not be looked up in the heap. The bounds/identity check also makes a
  // stale index harmless instead of an out-of-range access.
  const std::size_t index = e->heap_index;
  if (index == entry::npos || index >= heap_.size() || heap_[index] != e)
  {
    e->heap_index = entry::npos;
    return false;
  }

  pop_at(index);
  return true;
}

bool timer_queue::empty() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return heap_.empty();
}

timer_queue::clock::time_point timer_queue::next_expiry() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (heap_.empty())
    return (clock::time_point::max)();
  return heap_.front()->when;
}

std::shared_ptr<timer_queue::entry> timer_queue::take_expired(clock::time_point now)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (heap_.empty() || heap_.front()->when > now)
    return nullptr;

  std::shared_ptr<entry> expired = heap_.front();
  pop_at(0);
  return expired;
}

void timer_queue::swap_at(std::size_t a, std::size_t b)
{
  std::swap(heap_[a], heap_[b]);
  heap_[a]->heap_index = a;
  heap_[b]->heap_index = b;
}

void timer_queue::sift_up(std::size_t index)
{
  while (index > 0)
  {
    const std::size_t parent = (index - 1) / 2;
    if (heap_[parent]->when <= heap_[index]->when)
      break;
    swap_at(index, parent);
    index = parent;
  }
}

void timer_queue::sift_down(std::size_t index)
{
  const std::size_t size = heap_.size();
  for (;;)
  {
    const std::size_t left = index * 2 + 1;
    if (left >= size)
      return;

    std::size_t smallest = left;
    const std::size_t right = left + 1;
    if (right < size && heap_[right]->when < heap_[left]->when)
      smallest = right;

    if (heap_[index]->when <= heap_[smallest]->when)
      return;

    swap_at(index, smallest);
    index = smallest;
  }
}

void timer_queue::pop_at(std::size_t index)
{
  if (index >= heap_.size())
    return;

  const std::shared_ptr<entry> removed = heap_[index];
  const std::size_t last = heap_.size() - 1;

  // Move the last element into the hole. Do not use swap_at() here: it would
  // write the removed entry's heap_index back to a valid-looking value, which
  // is exactly the stale index that made a later remove() index out of bounds.
  if (index != last)
  {
    heap_[index] = std::move(heap_[last]);
    heap_[index]->heap_index = index;
  }

  heap_.pop_back();
  removed->heap_index = entry::npos;

  if (index < heap_.size())
  {
    sift_down(index);
    sift_up(index);
  }
}

} // namespace sasio
