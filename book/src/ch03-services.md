# io_context 为什么这么薄：service 注册表

第 2 章留下了一个悬而未决的问题：`async_read_some` 发起时，要把缓冲区和 handler 存进 socket，还要向 reactor 登记兴趣。可是 socket 从哪拿到 reactor？reactor 和定时器堆、handler 队列这些部件，谁创建、谁持有、什么时候销毁？

最省事的答案是把它们全塞进 `io_context`：

```cpp
class io_context {
  Scheduler scheduler_;    // handler queue
  Reactor   reactor_;      // descriptor table
  TimerHeap timers_;       // timers
  // ...one more line here for every new feature
};

```

能用。但看一下 asio 的 `io_context`——几百行的头文件里几乎没有一行属于某个具体功能，你找不到「reactor 成员」。不是 asio 偷懒，是它把这个问题交给了另一层：**service 注册表**。这层的存在，解释了 `io_context` 为什么可以这么薄。

## 设计：一个按需自装配的容器

sasio 的答案在两个类型里。`execution_context` 是容器，`service` 是部件的基类：

```cpp
class service {
public:
  virtual ~service() = default;
  virtual void shutdown() {}   // called at context destruction, reverse order
};

class execution_context {
public:
  template <typename T, typename... Args>
  T& use_service(Args&&... args);          // return if present, create if not

protected:
  void shutdown_services();                // reverse-order shutdown, then destroy
};

```

两条不变量，各管一件事：

1. **每个服务类型，每个 context 至多一份**。第一次 `use_service<reactor>()` 创建它，之后所有调用返回同一个实例。谁先来谁触发创建——socket 构造时、scheduler 构造时、用户代码里，都行；
2. **shutdown 按创建逆序，随后统一销毁**。后创建的服务先 shutdown。scheduler 构造时创建了 reactor，那么收尾时 reactor 先 shutdown、scheduler 后 shutdown——依赖者先走，被依赖者善后。

`io_context` 继承 `execution_context`，然后只做一件事：构造时向容器要一个 scheduler。队列、reactor、定时器堆都是服务，按需出现，`io_context` 对它们的存在一无所知。加一个新功能（比如第 10 章的 strand、第 9 章的 resolver）不需要碰 `io_context` 一行代码。

## 一次构造的递归链

「按需创建」最有趣的时刻在 `io_context` 构造：

```cpp
io_context::io_context()
  : scheduler_(use_service<scheduler>())   // first service in the container
{
}

```

这一行触发的不止一次创建。scheduler 管理着 reactor 和定时器堆，它的构造函数也向容器要这两个服务：

```cpp
scheduler::scheduler(execution_context& ctx)
  : reactor_(ctx.use_service<reactor>()),   // recursion: create the reactor
    timers_(ctx.use_service<timer_queue>()) // recursion: create the timer heap
{
}

```

于是第一个 `io_context` 的构造序列是：scheduler 注册进容器 → scheduler 构造 → reactor 注册 → reactor 构造（初始化 Winsock、开 interrupter）→ timer_queue 注册 → 构造完成 → 回到 scheduler → 回到 io_context。一条递归链，装配顺序恰好是依赖顺序——不需要任何「初始化列表」式的全局安排，谁需要谁触发。

这条链同时解释了不变量 2 为什么重要。创建顺序是 scheduler → reactor → timer_queue，那么 shutdown 的逆序就是 timer_queue → reactor → scheduler：reactor 先 shutdown（关 interrupter 和 Winsock），scheduler 后 shutdown（清空队列、停止调度），最后容器统一销毁对象。如果顺序反了，scheduler 的 shutdown 里那句 `reactor_.interrupt()` 就会碰上已 shutdown 的 interrupter。

## 精读：use_service 的 20 行

整层机制的核心就一个函数。看 `include/sasio/execution_context.hpp` 的实现（注释保留原文的英文）：

```cpp
template <typename T, typename... Args>
T& use_service(Args&&... args)
{
  static_assert(std::is_base_of<service, T>::value,
      "a sasio service must derive from sasio::service");

  std::lock_guard<std::recursive_mutex> lock(mutex_);   // ①

  const std::type_index key(typeid(T));                 // ②
  const auto existing = services_.find(key);
  if (existing != services_.end())
    return static_cast<T&>(*existing->second);          // ③

  std::unique_ptr<T> owned(new T(*this, std::forward<Args>(args)...));
  T& created = *owned;                                  // ④
  services_.emplace(key, std::move(owned));
  order_.push_back(&created);
  return created;
}

```

四处各有一个理由：

- **① 递归锁**。普通 `mutex` 在第 ④ 行就死锁了——`new T(*this, ...)` 会调用 T 的构造函数，而 scheduler 的构造函数又调用了 `use_service<reactor>()`，再次进入同一个函数、再拿同一把锁。`recursive_mutex` 允许同一线程重复进入，递归构造链才走得通。这是「按需装配」的直接代价；
- **② type_index 做键**。`typeid(T)` 配上 `std::type_index` 就能放进哈希表，「scheduler 这类服务」和「reactor 这类服务」天然不同键。asio 用的也是 typeid，只是套了一层自己管理的注册结构；
- **③ 命中即返回**。「每类型至多一份」在这里成立——第二次请求拿到的是同一个对象的引用；
- **④ 构造时把 context 传进去**。每个服务的第一个构造参数都是 `*this`，服务因此能反向使用容器（scheduler 正是这样拿到 reactor 的）。创建完成后记进 `order_`——它是逆序 shutdown 的依据。

还有一处值得停顿：`services_` 存的是 `unique_ptr<service>` 基类指针，`use_service` 返回前再向下转型。多态把「具体有哪些服务」的知识全部推给了调用点，容器自身保持无知——这就是 `io_context` 薄的机理。

## 对照表

| 问题 | asio | sasio |
| --- | --- | --- |
| 容器 | `execution_context.hpp` | `include/sasio/execution_context.hpp` |
| 注册实现 | `detail/service_registry.hpp` | `use_service` 模板本身 |
| 服务基类 | `execution_context::service` | `sasio::service` |
| 创建 | `make_service` + typeid 键 | `use_service` + typeid 键 |
| 递归构造 | 支持（service_registry 递归加锁） | 支持（`recursive_mutex`） |
| shutdown / 销毁 | `shutdown_services()` 逆序 shutdown，随后统一销毁 | `order_` 向量逆序 shutdown，`services_.clear()` 统一销毁 |
| asio 独有 | `add_service` / `has_service`、`notify_fork`、`shutdown` 与 `destroy` 分离 | 无（sasio 没有对应需求） |

asio 的 service_registry 比 sasio 多几样设施：注册表用 `first_service_` 链表串起所有服务，支持事后 `add_service` 与查询 `has_service`，支持 `notify_fork`（进程 fork 之后重建内部状态），并且把「shutdown（释放资源）」和「destroy（销毁对象）」分成两步。不变的地方：每个服务类型每个 context 仍只一份——`basic_io_object.hpp` 里 I/O 对象构造时 `use_service` 拿到的是这个共享的 service，各对象只持有自己的 `implementation_`。「typeid 键 + 按需创建 + 逆序 shutdown」的骨架完全一致，读 asio 的 `service_registry` 时，你会认出这里的每一行。

## 练习

**实验**：利用注册表写一个自己的服务——统计这个 context 一共执行了多少个 handler。全部代码不超过 30 行：

```cpp
class handler_stats : public sasio::service {
public:
  explicit handler_stats(sasio::execution_context& ctx) : service(ctx) {}
  void shutdown() override { std::printf("handlers: %zu\n", count); }

  void on_ran() { ++count; }

private:
  std::size_t count = 0;
};

// usage: wrap each post
auto& stats = ctx.use_service<handler_stats>();
ctx.post([&stats] { stats.on_ran(); /* your real work */ });
ctx.run();   // prints handlers: 1

```

跑起来，确认打印出的数字和你 post 的次数一致。然后回答一个问题：如果两个 `io_context` 各挂一个 `handler_stats`，它们的计数会互相干扰吗？（提示：回去看不变量 1 的「每个 context 至多一份」。）

**bug 狩猎**：把 `execution_context.hpp` 里的 `recursive_mutex` 换成普通 `std::mutex`，重新编译、运行 ch1 的 `ex01_post`。先预测会发生什么、在哪一行发生，再运行对照。如果你预测的是死锁——锁在第几次进入时卡住的？栈回溯里那两层 `use_service` 分别是谁调用的？

---

服务容器搭好了，第一个服务 scheduler 也已经在位。但直到现在，我们只说了「队列里的 handler 会被 run() 执行」——run 循环的全文、work 计数规则、「没有活干」的精确判定，都还没展开。下一章拆 scheduler，那是全库最重要的 83 行。
