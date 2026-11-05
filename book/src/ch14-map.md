# 真 asio 阅读地图

到上一章为止，executor 与 completion token 两层都拆完了——sasio 的九章（ch3–11）已经把 asio 的引擎与组合层各拆了一遍。现在回答全书开头承诺的最后一件事：拿着这些理解，怎么读 169,063 行的真 asio。

本章是前面章节六段骨架的唯一例外，改用三段结构：先讲 asio 源码树的**分层约定**（看懂目录就少走一半弯路），再给一份 **6 周阅读计划**（每周目标明确、可验收），最后是一张**符号映射清单**——sasio 的每个概念在 asio 里的落点。记述版本固定为 1.38.2。

## 分层约定：目录即说明书

`include/asio` 的组织有四条约定，按它们找文件事半功倍：

1. **`asio/xxx.hpp` 是门面，`asio/impl/xxx.hpp` 是实现**。`io_context.hpp` 1,384 行看着吓人，其中大半是文档注释与 `#include`；真正的逻辑在 `detail/impl/scheduler.ipp`（745 行）。见到 `impl/` 就知道「实现住这」；
2. **`detail/` 是引擎，`asio/` 是 API**。`detail/` 下的代码可以随意重构、改名、按平台分支，公开 API 不受影响；`asio/` 下的任何头文件都是承诺。前十一章对照过的 scheduler、reactor、timer_queue、reactive_socket_*_op 全在 `detail/`；
3. **`* _fwd.hpp` 只声明**。前向声明被大量使用来打断头文件依赖，读一个头时被「不完整类型」卡住，去 `* _fwd.hpp` 找它的真身声明在哪个头；
4. **按平台分文件**。同名的 `epoll_reactor` / `kqueue_reactor` / `select_reactor` / `dev_poll_reactor` / `win_iocp_io_context` / `io_uring_service` 是同一个概念在不同平台的后端，读一个就懂六个——它们实现的是同一组接口（reactor 语义或 IOCP 语义）。

另外两处「非文件的组织」：**operation 对象**（`detail/scheduler_operation`）是 asio 的万能信封——handler、allocator、work 单位、取消状态打包成一个堆上对象，在 op_queue 里排队、由 scheduler 执行后销毁。sasio 用成员槽位 + `std::function` 代替了它，第 7、8 章讲过这个等价与代价。**handler tracking**（`detail/handler_tracking.hpp`，90 行）是调试利器：定义 `ASIO_ENABLE_HANDLER_TRACKING` 后，每次发起与完成都打日志（`@asio|时间戳|动作`），排查「这个 handler 到底跑没跑」时比调试器快得多。

## 6 周阅读计划

每周 2–4 个文件、4–8 小时。每周有明确的「完成判据」，达不到就停在那周继续，别赶进度。

**第 1 周：io_context 与 scheduler**（对照本书 ch3–4）

- 主读：`asio/io_context.hpp`（1,384 行，跳过文档注释）、`detail/impl/scheduler.ipp`（745 行，重点 `do_run_one`）；
- 判据：能回答「两个线程同时 `run()`，谁执行哪个 handler、`stop()` 之后队列里的 handler 去哪了」；
- 自测：`outstanding_work_` 与 sasio 的四条件判定怎么对应？sasio 的 `maybe_stop_if_idle` 对应 asio 的哪几行？

**第 2 周：reactor（挑一个后端读透）**（对照 ch5）

- 主读：`detail/epoll_reactor.hpp`（314 行）+ `detail/impl/epoll_reactor.ipp`（838 行，重点 `start_op` / `run` / `wait`）；Linux 之外读 `select_reactor`（结构与 sasio 的快照模型最接近）；
- 判据：能解释「新注册为什么（或为什么不需要）唤醒等待线程」「speculative read 是什么」（asio 的投机读：就绪时直接试着做 I/O，失败再登记——比 sasio 的「移出再登记」更激进）；
- 自测：sasio 的 `if (first) interrupt()` 在 epoll_reactor 里对应什么？（答案：epoll 的内核态注册让新描述符天然可见，对应的是 `epoll_ctl` + `op_queue` 的入队，而不是唤醒。）

**第 3 周：socket 操作**（对照 ch7）

- 主读：`detail/reactive_socket_recv_op.hpp`（218 行）——一个完整的 operation 对象样板；`detail/socket_ops.hpp`（428 行）+ `detail/impl/socket_ops.ipp`（4,712 行，按需查阅，别通读）；
- 判据：能画出 `async_read_some` 从发起到完成的对象图（socket → service → reactor op → scheduler 队列）；
- 自测：operation 对象里除了 handler 还有什么？（allocator、work、cancellation slot——sasio 的槽位少了哪几个，少了会怎样。）

**第 4 周：组合操作与完成条件**（对照 ch8）

- 主读：`asio/read.hpp`（1,553 行，看发起函数怎么写）+ `asio/impl/read.hpp`（850 行，`detail::read_op`）；`write.hpp` / `impl/write.hpp` 对称（1,532 / 743 行）；
- 判据：能解释 `transfer_exactly(n)` 与 `transfer_at_least(n)` 的判定差异，以及完成条件在 op 的哪一步被询问；
- 自测：sasio 练习里让你写的 `transfer_at_least(n)`，asio 的 `base_from_completion_cond` 是怎么把它做成策略的？

**第 5 周：strand 与 executor**（对照 ch10、ch12）

- 主读：`asio/strand.hpp`（579 行）+ `detail/impl/strand_service.ipp`（211 行，全文短，值得通读）；`asio/associated_executor.hpp`；
- 判据：能回答「strand 的 handler 在哪个线程执行、`dispatch` 的就地条件、与 sasio 的 `can_dispatch() && !running` 怎么对应」；
- 自测：sasio 的 `thread_local current_strand_impl` 在 asio 里的对应物是什么？（executor 的调用栈比较——`running_in_this_thread` 走 associated executor。）

**第 6 周：协程**（对照 ch11）

- 主读：`asio/awaitable.hpp`（163 行）+ `asio/impl/awaitable.hpp`（1,121 行，重点 `awaitable_frame` 的 `resume` / `destroy`）；`asio/co_spawn.hpp`（525 行）+ `impl/co_spawn.hpp`（449 行）；
- 判据：能回答「协程帧的内存谁管（自定义 `operator new`）、co_spawn 的 work 谁管（`co_spawn_state` 的 guard）、`use_awaitable` 的 token 怎么变成 completion handler（`async_result` 特化，对照 ch13）」；
- 自测：sasio 的 `~promise_type` 调 completion，asio 的对应动作在哪？

六周之外的扩展阅读按需自取：`detail/handler_tracking.hpp`（90 行，调试）、`io_uring_service`（最新后端）、`asio/execution/*.hpp`（属性框架全文，ch12 的深水区）、`src/examples/`（官方示例目录，每个主题都有可运行版本）。

## 符号映射清单

读完 sasio 之后，这些概念在 asio 里的落点应该「一眼认出」：

| sasio 概念 | asio 落点（1.38.2） |
| --- | --- |
| `execution_context` / `service` | `asio/execution_context.hpp` + `detail/service_registry.hpp` |
| `io_context::work` | `executor_work_guard` / `make_work_guard` |
| `scheduler::run_one_impl` | `detail/impl/scheduler.ipp` 的 `do_run_one()` |
| `handler_work` | `work_cleanup`（operation 完成时的工作释放） |
| `reactor::start_read` / `if (first)` | `select_reactor::start_unready_op` 的 `if (first) interrupter_.interrupt()` |
| `timer_queue::add` 返回 earliest | `select_reactor::schedule_timer` 的 `if (earliest)` |
| `timer_queue::pop_at` | `detail/timer_queue.hpp` 的堆删除（无此 bug；asio 用 per_timer_data 链表） |
| `start_read_some` / `do_read` | `basic_stream_socket::async_read_some` → `detail/reactive_socket_recv_op.hpp` |
| `read_op` / `write_op` 接力 | `impl/read.hpp` / `impl/write.hpp` 的 `detail::read_op` / `write_op` |
| `can_dispatch() && !running` | `strand_service::do_dispatch` 的 `can_dispatch() && !locked_` |
| `drain()` 循环 | `strand_impl::run()` |
| `awaitable<T>` 的 `promise_type` | `impl/awaitable.hpp` 的 `awaitable_frame` |
| `co_spawn` 的 `top_level` + completion | `impl/co_spawn.hpp` 的 `co_spawn_state` |
| resolver 的短生命周期线程 | `detail/resolver_thread_pool.hpp`（常驻线程池） |

这张表也是本书的验收标准：**每一行你都能不看书、直接在 asio 源码里找到**，这本书就完成了它的任务。

## 最后的话

sasio 不是 asio 的缩水版，而是它的**讲解载体**：引擎三章（ch4–6）告诉你 asio 为什么停不下来又不会空转，I/O 三章（ch7–9）告诉你「发起与完成」为什么要分两段，strand 与协程两章（ch10–11）告诉你回调的两大痛点怎么解决，Part V 的三章（ch12–14）把被砍掉的通用化机械一件件还给你。

读完之后，值得常回来看看的只有两样：asio 源码本身（它是唯一的真值来源），和你自己在练习里写下的预测——错了的那些尤其值得留。异步编程的难点从来不是 API，是「同时发生的两件事之间发生了什么」；这本书教的所有东西，都是在帮你把这个「之间」看得更清楚。
