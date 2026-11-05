# 附录 D：练习答案

各章练习的完整答案。先自己做——答案不会跑掉。

## ch1 实验：构建与测试

命令见第 1 章正文；常见失败：Ninja 缺失时改用默认生成器（可执行文件移到 `build\Debug\`）；`ws2_32` 链接失败说明编译器环境没进 VS 命令行。

## ch1 bug 狩猎：异常用例三问

1. 抛异常的 handler **已出队**，它的异常与队列无关；队列里剩下的正常 handler 原样保留。catch 后重跑 `run()`，它们继续执行；
2. `stopped()` 是 **false**。异常不是停止指令：停止只发生在「四条件全空」或显式 `stop()`。异常飞出时队列里还有活，停止标志从未置位；
3. 每轮 `run()` 处理一个 handler 后要么正常耗尽、要么被异常打断；队列里有 3 个正常 handler，所以恰好在第三轮之后队列为空、四条件全空、`run()` 正常返回 0，循环 `break`。

## ch2 实验：echo 服务器并发

两个连接能同时被伺候：两个 socket 各自登记在 reactor 的描述符表里，一次 `WSAPoll` 报多个就绪，循环逐个处理。只连不发的连接不产生就绪，不浪费系统调用。

## ch2 bug 狩猎：cancel 的通知时机

handler 不会在 `cancel()` 的调用栈里执行——它在 `post` 之后由 run 循环执行，错误码为 `operation_aborted`。如果在调用栈里同步调用，而 handler 内部又碰了调用者正拿着的状态，就是重入；「取消必经 post」把这条路径排掉。

## ch3 实验：handler_stats 服务

两个 context 各挂一个 `handler_stats` 时互不干扰：不变量 1 的完整表述是「每个**服务类型**、每个 **context** 至多一份」——服务的键是 `(context, type)`，不是全局的 type。

## ch3 bug 狩猎：recursive_mutex

换成普通 `std::mutex` 后死锁发生在**第二次** `use_service` 进入时：`io_context` 构造 → `use_service<scheduler>()` 拿锁 → scheduler 构造 → `use_service<reactor>()` 再拿同一把锁。栈回溯里能看到两层 `use_service` 和一层 `std::mutex::lock`。递归构造链是按需装配的直接代价。

## ch4 实验：work 泄漏

卡住的循环：`work_count_` 为 1（guard 持有），队列在 handler 跑完后为空，定时器堆空，无描述符——但 `has_work_locked()` 因 `work_count_ != 0` 为真，`run_one_impl` 每圈走到第 3 段、算出「还有活」，睡进 `reactor_.run_for(max)`，无人唤醒。`reset()` 版本：第二个 context 四条件在 `reset()` 后全空，`run()` 返回 **0**。

## ch4 bug 狩猎：异常三问

见第 1 章答案（同一用例）；要点回顾：出队的 handler 与队列解耦、异常不置停止标志、3 个正常 handler 分布在 3 轮里执行。

## ch5 实验：复现第一次失守

注释两处 `if (first) interrupt()` 后，`tcp_multithread_wakeup_test` 约 5 秒失败：run 线程睡在 `WSAPoll(∞)`，新注册的读操作不在它的快照里，唯一的唤醒源是 5 秒后的看门狗定时器。还原后秒过。教训：快照模型的新等待来源必须唤醒等待者。

## ch5 bug 狩猎：spurious readiness

poll 说可读、`recv` 返回 `would_block` 的两类原因：**另一个读者抢先消费**（同一数据报被别的 `recvfrom` 取走、或引擎的投机读先做了一次 I/O），或**假警报本身**（poll 的语义只承诺「可能可读」）。TCP 的重传/校验不会让已报告的就绪消失，不在原因之列。不重新登记的后果：回调已被移出表（不变量 2），这个读操作从 reactor 的世界里消失——缓冲区永远等不到下一次就绪通知，**一次静默的读挂起**。

## ch6 实验：pop_at 越界

复现要点：两个定时器（50ms / 300ms），先到期的让堆执行 `pop_at(0)`，堆大小为 2、`index(0) != last(1)`，swap 路径把被删 entry 的 `heap_index` 写成 1；`pop_back` 后 `heap_.size() == 1`，脏索引 `1 == size()`。断言在**之后**触发：两个定时器对象析构时 `cancel()` → `remove()` 拿脏索引查堆，Debug STL 的 `vector subscript out of range` 弹出。爆发点远离出错位置，是索引类 bug 的典型特征。

## ch6 bug 狩猎：remove 失败的三种原因

- 索引为 `npos`：entry 已出堆——出堆只有一条路（`take_expired` 或 `pop_at`），两者的 handler 完成都已安排（成功完成由 scheduler 直接调用，正在投递的路上）；
- 索引越界：entry 已出堆（同上），或脏索引（修复后不可能存在；防御校验兜底）；
- 身份不符（`heap_[index] != e`）：索引属于另一个 entry——堆内交换时索引全程同步（`swap_at`），不会发生；作为防御保留。三种情况下 handler 的完成都已有归属，不会丢。

## ch7 实验：两个 session 的交错

会交错。每个 session 的每次 `async_read_some` 完成是独立的队列项；两个连接的 socket 同时就绪时，`WSAPoll` 一次报两个，`do_read` 依次在同一个线程执行，然后两个完成 handler 依次入队执行。单线程 `run()` 的并发模型是「交错」而非「并行」。

## ch7 bug 狩猎：close 后的新发起

`close()` 已把 `handle_` 置 `INVALID_SOCKET`，新的 `async_read_some` 走 `start_read_some` 的 `!is_open()` 分支：handler 以 `bad_descriptor` 经 `post` 投递。顺序：先投递的 `operation_aborted` 在队列前（post 先进先出），后投递的 `bad_descriptor` 在后——**队列 FIFO 保证了顺序**。旧通知（aborted）与新通知（bad_descriptor）都会到达，各自的缓冲区有效性由调用方负责。

## ch8 实验：状态移入了 write_op

手写版的 `write_offset_`/`write_size_` 是 session 的成员——每个 session 一份，生命期与 session 相同；组合版的对应状态是 `write_op` 的 `transferred_`——每次 `async_write` 一份，生命期由 shared_ptr 接力管理，链尾自动销毁。不需要 session 持有，因为它只属于这一次写操作。

## ch8 扩展：transfer_at_least(n)

`read_op` / `write_op` 加 `least_` 成员；完成判定从 `transferred_ == buffer_.size()` 改为 `transferred_ >= least_ || transferred_ == buffer_.size()`。`least_ = 0` 与默认行为的关系，按 asio 的**实际顺序**（`impl/read.hpp` 的 `read_op`）：每次被调用时**先**查完成条件（`operator()` 开头的 `check_for_completion`），I/O 之后若读到 0 字节则**直接 break 完成 eof**（I/O 之后的零字节分支），不经过第二次完成条件。因此：`least_ = 0` 时发起前条件即满足，返回成功 0 字节——但**并非一次 I/O 都不发**：完成条件通过后循环仍会发起一次零长度的 `async_read_some`，随后由零字节分支完成，不传输任何数据；EOF 场景 `least_ > 0` 报 eof 错误。实现时把完成条件放在 I/O 之前检查，才能对齐这个顺序。

## ch9 实验：UDP echo 与并发

`async_send_to` 未完成时，下一次 `async_receive_from` 已经登记——两个方向独立占槽，**可以并发**；这与 TCP 版（同一方向必须串行，写完才能再读的 session 逻辑）的差异来自 UDP 无流：收发互不依赖，引擎对两个方向独立登记。示例里的 `payload` 副本正是为此：共享缓冲会被下一个数据报覆盖。

## ch9 bug 狩猎：双时序

- 时序 A：采样发生在 `getaddrinfo` 返回后、post 前——`cancel()` 已把标志翻真，采样为 true → handler 收 `operation_aborted`（解析结果被丢弃）；
- 时序 B：采样后 `cancel()`——标志翻转不影响已捕获的 `aborted` 值 → handler 收解析结果。采样在**线程里**（而非 handler 里）的原因：post 是异步的，若在 handler 里采样，A/B 两种时序下「取消到达」与「采样」的先后将取决于调度延迟而非语义——取消通知会被无谓推迟一个队列周期，且时序 B 也可能被误报为取消。

## ch10 实验：绕过 strand

`s.post` 换 `ioc.post` 后，handler 直接进 scheduler 队列，两个 `run()` 线程各自取货执行，`max_active` 峰值远大于 1（具体数值随调度波动，典型 2）。对比 strand 版的恒定 1：strand 的 `running` 标志把并发压成串行。判别「哪些状态需要 strand」：多个 handler 会读写、且请求可能并发到达的状态。

## ch10 bug 狩猎：复现 dispatch 偏差

`!running` 版本下，`strand_dispatch_outside_run_test` 的**第一个断言**先失败（`ASIO_CHECK(!ran)`）——handler 在 `s.dispatch` 的调用线程（主线程）就地执行了。调试器调用栈从栈底读：测试函数 → `strand::impl::dispatch` → `execute` → handler，栈里**没有** `run` / `run_one_impl`——handler 根本不在 run 循环里。改回 `scheduler.can_dispatch() && !running`，两个断言都过。

## ch11 实验：递归链与栈深

递归 `chain(1000)` 全程栈深不涨：每层挂起后控制权交回引擎，子协程完成时 symmetric transfer 平移到父层，1000 次上交是 1000 次平移而非 1000 层调用栈（调试器在 deepest 断点看栈只有几帧）。改 `final_suspend` 为 `suspend_always`：链最深处的帧挂起后无人恢复（它不是 top_level、也没有 completion），整条链冻结；最深处定时器仍占着队列，但其余层永远等不到恢复——`run()` 在定时器完成后因四条件全空而停止，测试挂死或超时。这就是「没人收的帧就是泄漏」。

## ch11 bug 狩猎：move-only

编译错误的关键词是**右值引用 / `operator co_await() &&`**——awaitable 只接受右值。正确写法：`co_await std::move(task);`。move-only 更安全的原因：若允许拷贝，两个 awaitable 共享同一协程句柄，都传给 `co_spawn` 时——第一个 spawn 拿走帧并置 completion，第二个 spawn 又置一次 completion 并再次 `resume()` 一个已在跑/已销毁的帧。所有权唯一，销毁权才唯一。

## ch12 实验：两连跳

第一跳：`running_in_this_thread()` 为 1（手工包装生效）。第二跳：为 0——包装只包住第一跳的完成 lambda；第二跳是 lambda 内部直接发起的 `timer.async_wait`，其完成没有任何 strand 信息，落在任一 run 线程。asio 的 `bind_executor` 把 strand 写进 handler 的 associated executor，查找链逐层生效——这就是 sasio 手工包装与 asio 体系的差距。

## ch12 bug 狩猎：strand 的接口面

asio `strand.hpp` 除四个投递成员外还有 `query` / `require` / `prefer`（executor 属性协议的三件套）。缺了它们，以下代码移植不过来：`asio::query(strand, execution::context)`（取底层 context）、`require(strand, execution::blocking.never)`（调执行属性）、以及任何把 strand 当满足 executor concept 的对象传给泛型代码的写法。

## ch13 实验：read_with_future

10 行实现的本质：promise 与 future 的一次交接，handler 持 promise 的 `shared_ptr` 保活。asio 不为每个形式写 `read_with_xxx`，是因为 `async_initiate` 把「token 怎么变成 completion handler」交给了 `async_result` 特化——发起实现只写一次，新 token 类型自带新特化即可。

## ch13 bug 狩猎：use_awaitable 的恢复者

`asio/use_awaitable.hpp` 的特化里，completion handler 是一个持有 awaitable frame 引用的对象；被调用时它把结果写进 frame 并调用 `frame->resume()`——协程从 `co_await` 处恢复。sasio 的对应物是 `async_operation` 回调里的 `awaiting_.resume()`（ch11 第 ⑤ 步），同名兄弟的简化版。

# 附录 E：与 asio 的有意差异总表

按章汇总。完整清单以本仓库 README 的 `Deliberate limits` 列表为准（四份语言版本同步维护）；下表是书中展开过的条目及其去处。

| 章 | 差异 | 原因 | asio 去处 |
| --- | --- | --- | --- |
| 全书 | 只支持 Windows + WSAPoll | 教学范围 | 6 套后端见 `detail/` |
| 全书 | 无 executor / completion token | 组合层不进引擎 | ch12–13 的对照表 |
| ch4 | handler 用 `std::function` 而非 operation 对象 | 砍掉分配器/取消钩子 | `detail/scheduler_operation` |
| ch4 | 定时器与描述符不占 work 单位 | 计数只管队列 handler | `scheduler.ipp` 的 `outstanding_work_` |
| ch5 | interrupter 用自连 UDP | 省一对句柄 | `detail/select_reactor.hpp` 的 interrupter |
| ch6 | 单定时器单等待 | 免维护等待链表 | `detail/timer_queue.hpp` 的 per_timer_data |
| ch6 | 超时由 scheduler 计算 | 定时器独立成 service | `epoll_reactor` 的 `update_timeout` |
| ch7 | 每方向一格挂起（无 operation 队列） | 免对象管理 | `reactive_socket_recv_op` |
| ch7 | socket 不可移动、accept 传引用 | 免 move 语义 | `basic_stream_socket` |
| ch7 | 选项只有 `no_delay` | 够用即可 | `detail/socket_option*` 全家 |
| **ch9** | **UDP 截断不吞 `WSAEMSGSIZE`，以错误投递** | **未做 POSIX 对齐的吞错**（评审发现的行为差异） | `detail/impl/socket_ops.ipp` 的 `recvfrom` 对 `WSAEMSGSIZE` / `ERROR_MORE_DATA` 的处理 |
| ch9 | resolver 每查询一线程、取消仅最近一次 | 最小可懂版 | `detail/resolver_thread_pool.hpp` |
| ch9 | `udp::endpoint` 复用 `tcp::endpoint` | 免类型体操 | `asio/ip/udp.hpp` |
| ch8 | 组合操作只收单缓冲、完成条件硬编码 | 免 BufferSequence/策略基类 | `impl/write.hpp` + `base_from_completion_cond.hpp` |
| ch8 | 无 `async_read_until` | 动态缓冲+分隔符搜索，练习级 | `asio/read_until.hpp` |
| ch10 | strand 是对象不是 executor 适配器 | 无 executor 体系可适配 | `asio/strand.hpp` |
| ch10 | 无 `bind_executor` 自动继承 | 同上 | `asio/bind_executor.hpp` |
| ch11 | 协程无 token，完成写在 `~promise_type` | 免 `async_result` 体系 | `impl/awaitable.hpp`、`impl/co_spawn.hpp` |
| ch11 | `co_spawn` 直接 `resume()`（同步完成可在 run 外） | 如实呈现，差异登记 | `impl/co_spawn.hpp` 的投递启动 |

加粗行（ch9 的 UDP 截断）是评审流程发现并登记的行为差异，README 的 `Deliberate limits` 列表与本表同步。
