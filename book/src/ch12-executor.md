# executor 层：完成投递策略

第 10 章结束时留了一个钩子：sasio 的 strand 能把**你显式交给它的** handler 串行化，但 `async_write` 内部的中间完成不会自动跟着走——asio 里这叫「自动继承」，是 executor 层做的事。本章讲这层被 sasio 砍掉的东西：它解决什么问题、机制是什么形态、以及如果哪天要为 sasio 重新实现，最小需要哪些组件。

与前面十一章不同，本章的「精读」对象是真 asio 的符号——sasio 里没有对应物，这正是 Part V 的读法：**对着 asio 的实现理解概念，用 sasio 的缺位做对照**。

## 问题：投递策略是一等需求

回调式异步里有一个此前一直被略过的问题：handler 在**哪个线程、以什么方式**执行？引擎三章给的答案一直很朴素——「在 `run()` 线程里，从队列里取」。单线程程序里这没问题；多线程 `run()` 一开始，这个答案就不够用了。三个真实场景：

1. **串行化**：一个 session 的读写 handler 不能并发——这就是 strand，但组合操作的每一层中间完成也该进 strand，否则锁还是会被绕开；
2. **卸载**：解析完的耗时计算不该占着 I/O 线程——希望某个 handler「在别处跑」，跑完再回来；
3. **通用化**：每加一种执行场所（线程池、strand、自定义调度器），socket 类型就要跟着实例化一份——类型爆炸。

把这三件事归纳一下：**「在哪儿、怎么跑」需要成为 handler 的一等属性**，而不是引擎写死的规则。这个属性就叫 executor。

## 设计：三句话的框架

asio 的 executor 框架可以压缩成三句话：

1. **每个 handler 都有一个 associated executor**。怎么来的？一条两级查找链：handler 类型自带（自己的 `executor_type`，或对 `associated_executor` 的特化——`bind_executor(strand, h)` 产出的绑定对象就是后者）就用它；没有就用**调用方传入的默认值**——I/O 对象发起操作时把自己的 executor 作为这个默认值传入，而 I/O 对象的 executor 默认是 `any_io_executor`，包着它构造时拿到的那个（通常是 io_context 的）；
2. **投递动作有三种**：`post`（绝不就地，永远排队）、`dispatch`（可能就地——第 10 章的两条判据就是它的实现）、`defer`（与 post 类似，但当完成链是「刚被前一步投递下来」的续作时不抢跑，给引擎批处理的机会）；
3. **strand 是 executor 适配器**。`strand<Executor>` 包住一个内层 executor，`post`/`dispatch` 仍走向内层，只是多了排队与串行化——所以 strand 套线程池、strand 套 strand 都能组合。

sasio 有 1 的残缺版（`thread_local current_scheduler` 扮演了「默认回落」）、2 的一半（post/dispatch，没有 defer）、3 的对象版（strand 绑 context 而非适配 executor）。缺的正是「把策略做成参数」的那一半。

## trace：一次绑定的传递

executor 的价值在第 8 章的组合操作里最清楚。看 asio 里这行代码的传递链：

```cpp
asio::async_read(socket, asio::buffer(buf),
    asio::bind_executor(strand, handler));

```

`bind_executor` 产出一个绑定对象：handler + strand 打包。此后：

1. `async_read` 内部构造 `read_op`，最终完成 handler 就是这个绑定对象；
2. 每一层的**中间完成**（`read_op` 完成后要发起下一小段）在向引擎投递时，都会询问当前 handler 的 associated executor——绑定对象自带 strand；
3. 于是每一层的中间完成也经 strand 排队，**整条链串行化**。

第 1 步发起一次，第 2 步逐层生效——「一次绑定，处处生效」的机理就是**查找链跟着 handler 走**，而不是跟着「谁在调用」走。sasio 的 ex04 手工包装只包住最外层 lambda，内层完成（如果有的话）就漏出去了——因为 sasio 的 handler 是裸的 `std::function`，没有地方挂「我的 executor 是谁」这个属性。

顺带把三种投递的差别钉死在场景上：I/O 完成走 post（不能就地——你在 reactor 回调的栈上，重入危险）；用户在 handler 里调 `dispatch` 发起下一步（允许就地——已经在一个 handler 的栈上了，省一次投递）；完成链的续作走 defer（不就地，但告诉引擎「我是续作」，引擎可以选择立刻接着跑而不是排到队尾——cache 局部性的优化空间）。

## 精读：查找链与类型擦除

两个 asio 符号撑起了整层。第一个是**查找链的实现**，`asio/associated_executor.hpp`：

```cpp
template <typename T, typename Executor = inline_executor>
struct associated_executor
{
  using type = typename detail::associated_executor_impl<T, Executor>::type;

  static type get(const T& t, const Executor& d = Executor())
  {
    return detail::associated_executor_impl<T, Executor>::get(t, d);
  }
};

```

`detail::associated_executor_impl` 是一个两级判别：T 有自己的 `executor_type` 就用它（对 `executor_type` 的特化），T 直接特化了 `associated_executor` 也用它（`void_t` 特化），否则回落到调用方给的默认值 `d`（主模板）。注意查找链只有两级——「I/O 对象的 executor」不是链上的一级，而是**调用方传进的默认参数**：socket 发起操作时把自己的 executor 当 `d` 传入。这个「trait + 回落」的形状在 asio 里到处都是——associated_allocator、associated_cancellation_slot 同款。读 asio 时看到「一层套一层的 trait」，骨架就是它。

第二个是**类型擦除**，`asio/any_io_executor.hpp`：

```cpp
class any_io_executor :
  public execution::any_executor<
      execution::context_as_t<execution_context&>,
      execution::blocking_t::never_t,
      execution::prefer_only<execution::blocking_t::possibly_t>,
      execution::prefer_only<execution::outstanding_work_t::tracked_t>,
      execution::prefer_only<execution::outstanding_work_t::untracked_t>,
      execution::prefer_only<execution::relationship_t::fork_t>,
      execution::prefer_only<execution::relationship_t::continuation_t>
    >

```

注意它是一个**类**、继承自 `execution::any_executor<...>`（属性名的写法是 `blocking_t::never_t` 这种嵌套形式），七项属性把「不许阻塞」「work 计数」「续作关系」都变成可查询/可修饰的键。一个能装下任何满足这些属性的 executor 的擦除容器：内层用小对象优化存储，调用时走虚表。1.74 起它是所有 I/O 对象的默认 executor 参数——`basic_stream_socket<tcp, any_io_executor>` 一份类型通吃 io_context、thread_pool、strand。代价写在明处：每次投递多一层间接调用，小对象装不下时还有一次堆分配。asio 用它换掉了模板爆炸；sasio 只有一个 io_context，连这层都不需要。

## 三栏对照：缺位 ↔ 实现 ↔ 去处

| 能力 | sasio 现状 | asio 实现 | 去 asio 哪里找 |
| --- | --- | --- | --- |
| 完成投递到指定执行体 | 无（handler 裸 `std::function`，完成固定投给 scheduler） | associated executor 查找链 + `bind_executor` | `asio/associated_executor.hpp`、`asio/bind_executor.hpp` |
| strand 串行化 | 有（对象版，绑 context） | executor 适配器，可组合 | `asio/strand.hpp`、`detail/impl/strand_service.ipp` |
| 卸载到线程池 | 无 | `thread_pool`（本身就是 executor） | `asio/thread_pool.hpp` |
| 运行时选择执行体 | 无（只有一个 io_context） | `any_io_executor` 类型擦除 | `asio/any_io_executor.hpp` |
| 执行属性（不许阻塞、续作批处理等） | 无 | `execution::` 属性框架 | `asio/execution/*.hpp` |
| 续作投递（defer） | 无 | `defer` + continuation 语义 | `asio/defer.hpp` |
| 就地投递 | 有（两条判据，ch10） | `can_dispatch()` + `dispatch` | `detail/impl/scheduler.ipp` |

上表里每一行「sasio 现状」为「有」的，实现都已在前面章节拆过——引擎层的两行（strand、dispatch）恰是 executor 框架里**不需要参数化也能成立**的部分。这反过来解释了 sasio 的取舍：引擎主干（ch3–11）没有 executor 也自洽，需要它的全是「通用化」需求。

## 练习

**实验**：用 ex04 的手工包装证明「不自动继承」。在 ex04 的基础上构造一个两连跳场景：

```cpp
auto bound = bind_strand(strand_, [&](error_code) {
  std::puts("first hop: strand=1");
  const auto hop1_thread = std::this_thread::get_id();

  timer_.async_wait([&](error_code) {      // hop two: unwrapped
    const bool in_strand = strand_.running_in_this_thread();
    const bool same_thread = std::this_thread::get_id() == hop1_thread;
    std::printf("second hop: strand=%d, same_thread=%d\n",
        in_strand ? 1 : 0, same_thread ? 1 : 0);
  });
});
timer_.async_wait(bound);

```

预测：第一跳 `running_in_this_thread()` 为 1（手工包装生效）；第二跳为 0——包装只包住第一跳的完成，`timer.async_wait` 的完成没有任何 strand 信息。`same_thread` 在双 run 线程下通常为 0（另一线程拾取），单线程下可能为 1——但即便同线程，脱离的也是 strand 而不是线程：两者的判据不同（第 10 章）。

**bug 狩猎**：打开 `asio/strand.hpp`，数一数这个 executor 适配器暴露的接口面：`post` / `dispatch` / `defer` / `running_in_this_thread` 之外，还有 `query` / `require` / `prefer` 三个成员。对照 `sasio::strand` 的四个成员，列出 sasio 缺的部分——然后回答：缺了 `query` 之后，哪些 asio 代码移植不过来？（提示：`asio::query(s, execution::context)` 这类「问 strand 要它的底层 context」的代码。）

---

executor 解决了「在哪儿跑」，还剩「结果怎么交回」——同一层里的另一半。completion token 与 `async_result` 体系，下一章拆。
