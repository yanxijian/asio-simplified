# 时间：timer_queue 与 steady_timer

第 4 章的循环有两个问题要问定时器：「下一个什么时候到期」（决定睡多久）、「现在有没有到期的」（决定要不要干活）。第 5 章的唤醒路径里又多了一个：「新来的定时器是不是最早的」（决定要不要叫醒睡者）。三个问题都由 `timer_queue` 回答，数据结构是教科书级的最小堆——但这个不到 140 行的文件里，埋着全书唯一一个被 asio 自家测试抓出来的真 bug。本章拆它。

## 设计：堆、索引、生命周期

一个待办定时器在 `timer_queue` 里就是一个 `entry`：

```cpp
struct entry
{
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  clock::time_point when;                    // expiry moment
  std::function<void(error_code)> handler;   // who to call
  std::size_t heap_index = npos;             // my position in the heap
};

```

堆本体是 `std::vector<std::shared_ptr<entry>>`，按 `when` 排成最小堆。两条不变量：

1. **堆序成立，且每个在堆里的 entry 的 `heap_index` 与它的实际位置一致**；不在堆里的 entry，`heap_index` 必须是 `npos`。这两半缺一不可——前者保证 `front()` 永远是最早到期者，后者保证「取消」不会拿着过期索引乱摸；
2. **取消的完成永不就地调用**。`cancel()` 无论在哪被调用（哪怕在另一个 handler 的调用栈里），被取消的 handler 只会经 `post` 投递——重入是异步代码最阴的雷，这条不变量把它排掉。

`entry` 用 `shared_ptr` 持有，是因为它**同时活在两个世界里**：堆里有一份引用，定时器对象（`steady_timer`）的 `pending_` 里有一份。谁先动手谁决定命运——`cancel()` 抢先拿到锁就把 handler 挪出来投递取消；`take_expired` 抢先就把 handler 挪出来投递成功。互斥锁把这段竞争窗口收窄到原子，两边都不会见到「半完成」的中间状态。

## 精读：pop_at 与那个真 bug

堆的核心操作是「删掉任意一个位置的元素」。删除堆顶（`take_expired` 调用 `pop_at(0)`）和取消（`remove` 调用 `pop_at(任意位置)`）共用同一段代码：

```cpp
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
  removed->heap_index = entry::npos;      // ← the fix: leaving the heap is npos

  if (index < heap_.size())
  {
    sift_down(index);
    sift_up(index);
  }
}

```

这段注释里的「Do not use swap_at() here」是整章的关键。堆的经典实现里，「删任意元素」的写法是：把最后一个元素换到洞位，再 `pop_back`，然后在新位置上下各筛一次。顺手偷懒复用已有的 `swap_at`：

```cpp
// the buggy version:
swap_at(index, last);    // swap_at rewrites BOTH entries' heap_index!
heap_.pop_back();

```

`swap_at` 的职责是维护堆序，它忠实地把**两个**元素的索引都更新了——包括那个马上要被删除的 entry。删完之后，`removed->heap_index` 指着一个不再合法的位置。看具体数字：堆里两个定时器，取消堆顶。`pop_at(0)`：`index=0`，`last=1`，交换后被删 entry 的 `heap_index` 变成 1；`pop_back` 之后堆的大小变成 1。此时 `removed->heap_index == 1 == heap_.size()`——越界值落定。

什么时候触发？这个 entry 由 `steady_timer` 的 `pending_` 持有引用，定时器对象析构时调用 `cancel()`，`cancel` 调 `remove`，`remove` 拿着 `heap_index = 1` 去查大小为 1 的堆——`heap_[1]` 越界，Debug 版 STL 的断言当场弹出（`vector subscript out of range`）。

为什么这个 bug 能留下来？因为它需要**两个**定时器才触发：只有一个定时器时 `index == last`，不交换、不越界，删空堆后一切正常。最初的自测用例恰好只用了一个定时器。移植 asio 自家的 `deadline_timer` 测试时，用例里并排放着两个定时器，Debug 断言第一时间触发——这就是贯穿全书的「测试移植抓真 bug」的来历。修复分两层：`pop_at` 不再用 `swap_at`，出堆即置 `npos`（根因）；`remove` 开头的索引做**边界 + 身份**双重校验——

```cpp
const std::size_t index = e->heap_index;
if (index == entry::npos || index >= heap_.size() || heap_[index] != e)
{
  e->heap_index = entry::npos;
  return false;
}

```

就算未来再出现脏索引，`remove` 也只会安静地返回「不在堆里」，不会越界。根因修复加防御性校验，两层各管各的。

同一个 `add` 还兼职第 5 章的唤醒信号——返回「新 entry 是否成为最早到期者」：

```cpp
const bool earliest = heap_.empty() || e->when < heap_.front()->when;
// ...push, sift up...
return earliest;      // steady_timer decides on wake_waiters() from this

```

一行返回值，把「快照模型的第二次失守」的修复从堆内部 cleanly 引出去——堆自己不知道 reactor 的存在，锁序（scheduler → timer → reactor）也因此不被破坏。

## steady_timer：等待、取消、析构

堆是引擎件，`steady_timer` 是用户直接操作的入口。三个操作各看一眼。

**`async_wait`**：把 `{expiry_, handler}` 打包成 entry，挂进堆。一个定时器对象同一时刻只允许一个挂起的等待——第二次 `async_wait` 会先 `cancel` 前一次（老等待带着 `operation_aborted` 完成，新等待入堆）。asio 允许同一对象上挂任意多个等待，这是 sasio 列在差异表里的简化。

**`cancel`**：把 `pending_` 的引用拿过来（`std::move`），请求堆移除。移除成功说明等待还没到期——把 handler 从 entry 里挪出来，`post` 一个带着 `operation_aborted` 的完成：

```cpp
// Never invoke the handler from inside cancel(): post it, exactly like asio.
scheduler_.post([handler = std::move(handler)]() mutable {
  handler(operation_aborted);
});

```

移除失败说明等待**已经到期、完成已在路上**（scheduler 的 `take_expired` 拿走了 handler）——`cancel` 返回 0，什么都不做。注意这个判定的锁纪律：`take_expired` 把 handler 移出 entry 与 `cancel` 的 `remove` 用同一把 `timer_queue::mutex_` 串行化，所以不存在「两边都拿到 handler」或「两边都没拿到」的中间态。不变量 2 在这里成立：无论 `cancel` 从哪个栈上发起，handler 都经由 `post` 在 run 循环里执行，绝不就地重入。

**析构**：`~steady_timer` 就是 `cancel()`——对象死了，挂起的等待以 `operation_aborted` 完成。asio 的语义相同；代价是析构一个「还在等待」的定时器会向队列注入一个完成 handler，所以**析构时机**要心里有数（这也是为什么 co_spawn 的适配器按引用持有定时器——协程活着，定时器必须活着）。

## 对照表

| 问题 | asio | sasio |
| --- | --- | --- |
| 堆本体 | `detail/timer_queue.hpp`（按时钟类型各一） | `src/timer_queue.cpp`（仅 steady_clock） |
| 定时器身份 | `per_timer_data`（每个定时器一条等待链表） | `shared_ptr<entry>`（每对象至多一个等待） |
| 多重等待 | 支持；`expires_at` 取消全部 | 不支持；新等待自动取消旧等待 |
| 取消粒度 | `cancel` / `cancel_one` | `cancel` |
| 完成路径 | 完成 op 进 scheduler 队列 | 到期直接调用（scheduler 循环内）；取消经 `post` |
| 与 reactor 的关系 | timer queue 注册给 reactor，reactor 算 poll 超时 | timer queue 独立成 service，scheduler 算 poll 超时 |

两处差异值得多说一句。其一，asio 把 timer queue 挂在 reactor 里、由 reactor 计算 poll 超时，sasio 挂在 scheduler 里、由 scheduler 计算——位置不同，「超时是快照、新最早定时器要唤醒」的教训两边都吃过（asio 的 `schedule_timer` interrupt 与 sasio 的 `wake_waiters`）。其二，多重等待在教学版里的缺席不是偷懒：它的实现价值在「一个定时器对象作为多个等待的公共句柄」，而 sasio 的协程适配器（第 11 章）从不这么用——每处 co_await 一个独立定时器，单等待足够。

## 练习

**实验**：亲手复现 `pop_at` 的越界 bug。打开 `src/timer_queue.cpp`，把 `pop_at` 改回注释里描述的旧实现（用 `swap_at(index, last)`，删掉 `removed->heap_index = entry::npos;` 那行），再写一个 10 行的双定时器用例：

```cpp
sasio::io_context ctx;
sasio::steady_timer t1(ctx), t2(ctx);
t1.expires_after(std::chrono::milliseconds(50));
t1.async_wait([](sasio::error_code) {});
t2.expires_after(std::chrono::milliseconds(300));
t2.async_wait([](sasio::error_code) {});
// t1 expires → pop_at(0) takes the swap path → the other entry's index goes dirty
// t1/t2 destruct → cancel → remove reads the dirty index → Debug assert fires

```

用 Debug 版构建运行，看断言弹出的时机和调用栈——注意出错的位置不在 `pop_at`，而在**之后**某次 `remove` 的 `heap_[index]`。把代码改回正确实现，断言消失。这个练习给你的是一种直观感受：堆这种「索引与位置必须时刻一致」的结构，脏索引的爆发点永远离出错位置很远。

**bug 狩猎**：`steady_timer::cancel` 里，`timers_.remove(entry)` 返回 false 时函数直接 `return 0`——不投递任何东西。可此时 entry 的 handler 还留在 entry 里，而 entry 可能马上随 `pending_` 一起被丢弃。handler 会不会就这么丢了、永远不被调用？（提示：`remove` 失败有三种原因——索引是 npos、索引越界、身份不符。逐个原因想一遍：每种情况下，handler 的完成由谁负责？答案藏在 `take_expired` 拿锁的时机里。）

---

到这一章为止，引擎的三大件——队列、就绪、时间——全部拆完，三条唤醒路径全部到位：两条失守被测试抓住并修复，一条（post）从第一版就正确。下一章开始用这套引擎做真正的事：`socket` 的 `async_read_some` 发起时「存状态、登记兴趣」，就绪后 `do_read` 才碰系统调用——发起函数与完成路径的两段式，是 asio 所有 socket 代码的共同形状。
