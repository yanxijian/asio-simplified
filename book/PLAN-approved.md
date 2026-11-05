# 《Asio 伴读：从 4200 行读懂 16 万行》—— 书稿写作计划

> **状态**：待评审（`当前轮次：3/10`）
> **评审协议**：见本文第 10 节；评审员操作手册见同目录 `REVIEWER-PROMPT.md`。
> 本文件是书稿的基准计划，达成共识（或 10 轮上限后作者自主裁决）后更名为 `PLAN.md`，
> 此后每一批写作的评审都引用它。

---

## 1. 定位与已定决策

- **一句话定位**：以 asio-simplified（本仓库，约 4,200 行，实测 4,207）为解剖标本，
  讲透 asio 的异步引擎设计，
  并为直接阅读真 asio（chriskohlhoff/asio，本书记述版本固定为 **1.38.2**）铺路。
- **书名（工作名）**：《Asio 伴读：从 4200 行读懂 16 万行》。
- **读者画像**：会用 socket 与线程、写过回调风格异步代码的 C++ 开发者；不要求读过 asio；
  C++ 标准按 sasio 现状（C++17 为主，协程章 C++20）。
- **已定决策**（2026-11-04 与作者确认）：
  1. 独立 `book/` 目录 + mdBook 出 HTML；
  2. 教材向，全稿约 6–8 万字，附录带练习答案；
  3. executor 与 completion token **拆成两章**系统展开；
  4. 按 Part 分批写作，每批（含本计划）经独立评审循环后方可进入下一批；评审循环
     上限 10 轮，达到上限由**作者自主裁决**收尾——全书完成前无人工介入。
- **语言策略**：正文中文，代码与标识符英文，代码块内注释英文（与仓库注释规则一致）。
  英文版为批次 6；**日文版为批次 7、韩文版为批次 8**（2026-11-05 新增：书是静态成品，
  四语言一次到位后无同步腐坏风险，与仓库 README 四语言惯例对齐；届时在
  `repo-rules.json` 的 `markdown.translations.pairs` 增补全部成对清单）。

## 2. 与仓库现有内容的关系

| 现有内容 | 去向 |
| --- | --- |
| `docs/zh/executor-and-tokens.md`（93 行） | 第 12 章底稿（约 1/3 现成），改写时保留其「决策记录」语气与对照表风格 |
| `examples/ex04_bind_executor.cpp` | 第 10 章的 `bind_executor` 手工近似素材 |
| `README.md` 的 `Deliberate limits` 列表 | 附录 E 底稿（扩展为逐章差异地图） |
| `tests/asio/` 的移植来源注释与两个漏唤醒回归测试 | 第 5、6 章的 bug 狩猎素材 + 附录 C 素材 |
| `README.md` 的 `Reading order`（6 步） | 第 14 章「真 asio 阅读地图」的对照起点（6 步扩展为 6 周） |

## 3. 全书结构（5 部分 · 14 章 · 5 附录）

| Part | 章 | 主题 |
| --- | --- | --- |
| 0 开篇 | 1 | 为什么先读 4200 行的版本 |
| I 模型 | 2 | Reactor 与 Proactor：一次异步操作的完整生命周期 |
| II 引擎 | 3–6 | service 注册表 · scheduler · reactor · 定时器 |
| III I/O | 7–9 | socket/acceptor · 组合操作 · UDP 与 resolver |
| IV 串行与现代化 | 10–11 | strand · C++20 协程 |
| V 进入真 asio | 12–14 | executor 层 · completion token 层 · 真 asio 阅读地图 |
| 附录 | A–E | 术语表 · asio 源码树导览 · 测试套件导读 · 练习答案 · 有意差异总表 |

篇幅分配（目标）：ch1 约 0.4 万字；ch2–3 各 0.5 万；ch4–6 各 0.6 万；ch7–9 各 0.6 万；
ch10–11 各 0.6 万；ch12–13 各 0.7 万；ch14 约 0.6 万；附录合计约 0.8 万。总计约 7.4 万字
（在 6–8 万区间，允许 ±10%）。

## 4. 每章骨架模板（固定六段，先定死再动笔）

1. **问题**：没有这个组件世界会怎样——先立动机，不上代码；
2. **设计**：核心数据结构 + **不超过三条不变量**（可背诵的那几句话）；
3. **生命周期追踪**：一次调用的完整时序（mermaid sequenceDiagram）；
4. **代码精读**：只精读一段最关键的代码（≤80 行，逐段讲）；
5. **sasio ↔ asio 对照表**：砍了什么 / 为什么 / 去 asio 哪个文件找；
6. **练习**：一个动手实验 + 一个 bug 狩猎（教材向：附录 D 给完整答案）。

**Part V 变体**（ch12–14）：ch12–13 没有对应的 sasio 实现，第 4 段「代码精读」的对象
换成真 asio 的符号与片段；第 5 段对照表改为「sasio 缺位 ↔ asio 实现 ↔ 去处」三栏；
第 3 段与 Part II/III/IV 同款，用文字 trace 替代时序图。ch14 是阅读地图章，豁免
六段骨架，改用「分层导览 → 6 周计划 → 自测问题」结构。（修订 2026-11-05，
PART5 补充第 3 段形态说明）

**Part 0 变体**（ch1）：第 3 段「生命周期追踪」由「全书路线图」（组件依赖链）顶替；
第 4 段「代码精读」的对象是 sasio 的 hello world（`ex01_post` 的核心几行）；其余
四段照常。（变更 2026-11-05，PART1 评审 R.1.3）

**Part II 变体**（ch4–6）：第 3 段「生命周期追踪」用文字 trace 与代码骨架替代
时序图——第 2 章已给出完整时序图，引擎三章的「一次操作」就是 run 循环与
`run_for` 骨架本身，再画时序图会是重复。其余四段照常。（变更 2026-11-05，
PART2 评审 R.1.9，作者声明的偏差经评审确认成立）

**Part III 变体**（ch7–9）：同 Part II，第 3 段用代码化 trace 替代时序图
（ch7 直接引用第 2 章时序图的落点）。其余四段照常。（变更 2026-11-05，
PART3 评审 R.1.4）

**Part IV 变体**（ch10–11）：同 Part II/III，第 3 段用文字 trace 替代时序图。
其余四段照常。（变更 2026-11-05，PART4 评审 R.1.5）

## 5. 文风规范（动笔前定死，评审按此把关）

**总纲**：像资深同事结对讲解，不像教材宣读，更不像产品发布会。读者是聪明的成人，
不需要被"引领"。

1. **人称**：对读者用「你」。少用「我们」，禁用「笔者」。
2. **开头**：每章每节从具体场景或代码进入。禁用「本章将介绍」「让我们首先」「在深入
   XX 之前」式开头。
3. **段落为主**：正文以段落叙述推进；列表只用于真正并列 ≥3 项；表格只用于对照。
   正文不允许被 bullet 掏空。
4. **节奏**：句子长短交错；同一修辞（排比、设问、破折号）连续出现不超过 2 次。
5. **禁用词**：深入、赋能、优雅、强大、全面、完美、浅显易懂、综上所述、总结一下、
   值得注意的是、不难发现、本质上、众所周知。让数字与代码说话，不用形容词夸代码。
6. **比喻限额**：一个概念至多一个比喻；用过就回到实物和代码，不重复消费同一个类比。
7. **讲代码的次序**：先说「发生了什么」，再说「为什么这么设计」；禁止逐行复述代码。
8. **承认取舍**：sasio/asio 里不优雅的地方直说。承认「这里没有好办法，只有权衡」
   优于粉饰。
9. **术语策略**：核心术语保留英文原文——handler、work、strand、executor、completion
   token、reactor、proactor、operation、continuation——不造中文译名；每个术语首次
   出现时用一句话给中文注释，之后只用英文。io_context、WSAPoll 等专有名词同理。
10. **结尾不升华**：章节结尾要么留具体悬念（「下一个问题：谁来唤醒睡着的线程？」），
    要么直接进练习。禁止「本章我们学习了……」。
11. **自查**：写完每章随机读三段，听着像百科词条或发布会就重写。

**样段**（ch2 开头，目标文风的靶子）：

> 先看一段你最熟悉的代码：
>
> ```cpp
> char buf[1024];
> int n = recv(sock, buf, sizeof(buf), 0);
> ```
>
> `recv` 返回时，结果已经在手里：要么拿到了数据，要么对端关了，要么出错。
> 函数返回就是句号。这是同步。
>
> 现在把两个这样的调用放进同一个进程——`accept` 在等连接，`recv` 在等数据，
> 任何一个先阻塞，另一个就永远轮不到。同步的句号，在只有一条执行路径的程序里
> 就是死锁。异步要解决的就是这个：**发起之后先回来，结果到了再通知你**。
> 剩下的整本书，讲的其实只有一件事：这个「通知」是怎么被制造、排队和投递的。

**反例**（AI 腔对照，不许出现）：

> 在深入探讨异步编程的世界之前，我们首先需要理解一个重要的概念。异步编程是一种
> 强大的编程范式，它能够优雅地解决高并发场景下的性能问题。接下来，本章将全面
> 介绍 Reactor 与 Proactor 两种模式。

反例的病：四个禁用词、零事实、概述性空转、全文没有「你」。对照样段：第一行就是代码，
第三段已经在制造悬念。

## 6. 贯穿装置（三个，全书复用）

1. **Echo server 进化史**：第 7 章手写 `async_write_some` 循环 → 第 8 章换 `async_write` →
   第 10 章加 strand 双线程 → 第 11 章协程版。同一例子长出所有特性。
2. **「唤醒的两次失守 + 一次天生的正确」案例专栏**：首次描述符注册漏唤醒（chapter 2
   修复，对照 asio `select_reactor::start_unready_op`）→ 新最早定时器漏唤醒（chapter 1
   修复，对照 `select_reactor::schedule_timer`）。两次失守都有仓内回归测试作证
   （`tcp_multithread_wakeup_test`、`timer_multithread_wakeup_test`）。`post` 的唤醒
   不是失守——`scheduler::post` 从第一版起就无条件 `reactor_.interrupt()`，专栏以它
   开头交代「天生的正确」长什么样，再讲两条后补的。第 5 章集中讲，第 6 章回收。
3. **bug 狩猎练习**：`timer_queue::pop_at` 堆索引越界（asio 测试移植时抓到的真 bug）、
   Debug CRT 断言被 Ignore 后测试假通过（`unit_test.hpp` 的 `_Exit(3)` 防线）。

## 7. 逐章内容要点

### Part 0 · 开篇

**ch1 为什么先读 4200 行的版本**（约 0.4 万字）
- 问题：真 asio 约 600 个头文件、约 17 万行（`include/asio` 下 `.hpp`，1.38.2 实测
  600 个 / 169,063 行；含 `.ipp` 实现则约 19.7 万行）+ 6 套平台 I/O 后端
  （epoll、kqueue、select、dev_poll、IOCP、io_uring，另有 null_reactor、spin_reactor
  两个非平台桩）+ executor/completion token 组合层，第一屏就被淹没；
- 方法论：trace 一个操作 → 改代码验证 → 回 asio 找对应物（全书反复使用的三步）；
- 环境搭建：CMake + MSVC/Ninja，三分钟跑通 9 个测试；
- sasio 的取舍宣言：保留引擎、砍掉组合层（指向 `docs/zh/executor-and-tokens.md`）。

### Part I · 模型

**ch2 Reactor 与 Proactor：一次异步操作的完整生命周期**（约 0.5 万字）
- 同步 / 异步 / 半同步半异步谱系；IOCP 是真 proactor、epoll/select 是模拟；
- sasio 为什么在 Windows 上选 WSAPoll 而不是 IOCP（教学可读性）；
- **全书锚点：发起函数绝不内联执行 handler**——I/O 完成（read/write/connect/accept/
  resolve）一律经 `scheduler::post` 投递；到期定时器由 `scheduler::run_one_impl` 在
  run 循环内直接调用（不经队列）；`dispatch` 只在「当前线程已在处理这个 context」时
  就地执行。三句都可检验，ch4 的精读与 ch6 的定时器完成都按这个区分展开；
- 生命周期序列图：`async_read_some` 发起 → 注册 → WSAPoll 就绪 → `do_read` 系统调用 →
  `post` 完成 → handler 执行。

### Part II · 引擎

**ch3 io_context 为什么这么薄：service 注册表**（约 0.5 万字）
- 不变量：①服务按需创建且每个类型恰一个；②析构按创建逆序 shutdown；
- `use_service` 的递归构造（scheduler → reactor → timer_queue）；
- 用户自挂服务的扩展点；
- asio 对照：`execution_context.hpp` + `detail/service_registry.hpp`（模板与类型索引细节）。

**ch4 scheduler：handler 队列与 work 计数**（约 0.6 万字）
- 不变量：①每个入队 handler 携带一个 work 单位，跑完释放（异常也释放）；
  ②`work_count_` 为零、队列为空、定时器堆为空且无已注册描述符——`has_work_locked()`
  的四个条件全空——时 scheduler 自行 stop，`restart()` 后才能再跑。持有
  `io_context::work` 时即使后三者全空也不停；定时器与描述符不占 `work_count_`，
  但同样阻止 stop；
- 精读 `run_one_impl`（约 80 行，实测 83）：队列 → 到期定时器 → 算 poll 超时 →
  `reactor::run_for`；
- `handler_work` RAII ↔ asio `work_cleanup`；异常传播出 `run()` 的路径；
- `io_context::work` ↔ `executor_work_guard`；
- 练习：制造 work 泄漏，观察 `run()` 永不返回。

**ch5 reactor：就绪、唤醒与描述符表**（约 0.6 万字）
- 不变量：①poll 集合是快照，新注册必须唤醒等待线程；②就绪 handler 移出表后才执行，
  防止 level-triggered 重复投递；
- interrupter：自连 UDP socket ↔ asio 的 socket pair / eventfd；
- **「唤醒的两次失守 + 一次天生的正确」案例专栏**（贯穿装置 2 主体）；
- spurious readiness 的重注册（`do_read` 的 would_block 分支）；
- `idle()` 也是 work：描述符表非空时 `run()` 不退出；
- asio 对照：`detail/select_reactor.hpp`、`epoll_reactor.hpp` 的 start_op / schedule_timer。

**ch6 时间：timer_queue 与 steady_timer**（约 0.6 万字）
- 不变量：①堆序 + `heap_index` 与堆内容始终一致（或为 npos）；②取消的完成永不就地调用；
- 最小堆与稳定删除：`pop_at` 的 bug 故事（贯穿装置 3）；
- `cancel()` 经 `post` 投递 `operation_aborted`；析构即取消的语义与代价；
- 单 wait 简化 vs asio 的 per_timer_data 链表 + 多重 wait + `cancel_one`；
- 超时由 scheduler 计算而非 reactor——与 asio 的差异及原因。

### Part III · I/O

**ch7 socket：发起函数只描述「就绪后做什么」**（约 0.6 万字）
- 不变量：①每方向至多一个挂起操作（`already_started` 拒绝）；②read/write/accept 的
  发起函数只登记兴趣、不做数据面系统调用；connect 例外——发起时做一次非阻塞
  connect 尝试（`start_connect` 直接调 `connect_socket`），interrupter 的 send 属
  控制面，不算在内；
- `start_read_some`（存状态+注册）与 `do_read`（真正 recv）的两段式；
- non-blocking connect 的 `SO_ERROR` 流程；acceptor 与 `assign`；`close` → `cancel` 顺序；
- **为什么 asio 需要 operation 对象**（能活到事件发生的状态载体）——sasio 用成员槽位代替
  的代价与边界；
- asio 对照：`basic_stream_socket.hpp`、`detail/reactive_socket_recv_op.hpp`。

**ch8 组合操作：operation 套 operation**（约 0.6 万字）
- `read_op/write_op` 的 shared_ptr 生存链：状态恰好活到整条链结束；
- 部分读写推进（`transferred_`）与 eof 语义；
- 空缓冲的立即完成路径（经 `context().post`）；
- asio 对照：`impl/read.hpp` 与 `impl/write.hpp` 里的 `detail::read_op` / `write_op`、
  `detail/base_from_completion_cond.hpp`、`transfer_at_least` 全家——
  练习：给 sasio 加 `transfer_at_least(n)`。

**ch9 UDP 与 resolver**（约 0.6 万字）
- 同一 reactor 注册表的复用；`recvfrom` 写回 sender endpoint；
- resolver：同步 `getaddrinfo`；异步 = 每查询一个短命线程 + work 单位撑住 `run()`；
  cancel 完成时采样（`const bool aborted`）的语义；detach 线程的生命周期约束；
- asio 对照：常驻线程池 + 完整取消队列；`udp::endpoint` 复用 `tcp::endpoint` 的取舍。

### Part IV · 串行与现代化

**ch10 strand：一个队列 + 一个 running 标志**（约 0.6 万字）
- 不变量：①同一 strand 的 handler 不并发且 post 保序；②handler 抛异常不锁死 strand；
- 排空循环；dispatch 嵌套（thread_local 标记 ↔ asio 的 executor 比较）；
- 异常后重投递；`running_in_this_thread`；
- `bind_executor` 的手工近似（吸收 `examples/ex04_bind_executor.cpp`）及其
  「不能自动继承」的局限（指向 ch12）。

**ch11 协程：把回调写成直线**（约 0.6 万字）
- `awaitable<T>` 的 promise 三要素：值/异常/续作；
- `initial_suspend` 挂起、co_spawn `resume()` 启动；`final_suspend` 的顶层/嵌套两态
  （顶层不挂起 → 帧自动销毁 → `~promise_type` 交出完成 handler）；
- symmetric transfer；`async_operation` 把回调桥接成 awaitable；
- asio 对照：`awaitable_frame` + `use_awaitable` 为什么要整套 completion token 体系
  （指向 ch13）。

### Part V · 进入真 asio

**ch12 executor 层：完成投递策略**（约 0.7 万字）
- 以 `docs/zh/executor-and-tokens.md` 前半为底稿扩写：
  associated_executor 查找链、`bind_executor` 的一次绑定处处生效、strand 作为 executor
  适配器、`any_io_executor` 类型擦除的代价、`execution::` 属性概览（`blocking.never`、
  `relationship.continuation`）；
- 「什么时候值得给 sasio 长回去」：沿用现有成本估算（约 250–350 行），给出一条最小实现路径。

**ch13 completion token 层：结果获取方式**（约 0.7 万字）
- 以 `docs/zh/executor-and-tokens.md` 后半为底稿扩写：
  `async_result<Token, Signature>` 的特化机制、默认 completion token、
  `use_future` / `use_awaitable` / `deferred` / `detached` 各自怎么落地；
- 用 ch11 的协程适配器对照：`co_await async_wait(timer)` 与
  `timer.async_wait(use_awaitable)` 是同一条路的两端；
- 最小 token 实验设计（300–500 行成本）。

**ch14 真 asio 阅读地图**（约 0.6 万字）
- 约 17 万行的分层约定（`fwd.hpp` / `impl/` / `detail/`）；
- operation 对象与 op_queue；6 套平台 I/O 后端如何选择；handler_tracking；
- **6 周阅读计划**：每周目标文件 + 与本书章节的映射 + 自测问题；
- 「读完 sasio 你会认出什么」清单：每个 sasio 概念 → asio 落点（符号级）。

### 附录

- **A 术语表**：proactor、handler、work 单位、operation、continuation、strand……
- **B asio 源码树导览**：`include/asio` 顶层目录逐项说明，标注 sasio 对应物；
- **C 测试套件导读**：`unit_test.hpp` 宏、移植来源表、CRT 断言 `_Exit(3)` 机制、
  两个回归测试的判别力设计；
- **D 练习答案**：全部练习的完整答案（教材向要求）；
- **E 有意差异总表**：README 的 `Deliberate limits` 列表扩展为逐章差异地图
  （差异 → 原因 → asio 去处）。

## 8. Part 批次与验收标准

| 批次 | 内容 | 评审文档 |
| --- | --- | --- |
| 0 | 本计划 | `PLAN-needreview.md`（本文件） |
| 1 | mdBook 脚手架 + ch1、ch2 | `PART1-needreview.md` |
| 2 | ch3–ch6 | `PART2-needreview.md` |
| 3 | ch7–ch9 | `PART3-needreview.md` |
| 4 | ch10–ch11 | `PART4-needreview.md` |
| 5 | ch12–ch14 + 附录 A–E | `PART5-needreview.md` |
| 6 | 英文版翻译（全书完成后） | `PART6-needreview.md` |
| 7 | 日文版翻译（src-ja/，17 文件，2026-11-06 提交——原批次 7 拆分为日/韩两批） | `PART7-needreview.md` |
| 8 | 韩文版翻译（src-ko/，17 文件，批次 7 评审期间并行翻译） | `PART8-needreview.md` |

每批验收清单（作者自查 + 评审员复核共用）：

1. 所有对 sasio 代码的断言与仓内实际代码一致（符号、行为、语义）；
2. 所有对真 asio 的断言能在 1.38.2 源码中核实（按第 9 节的核对方式）；
3. 术语、贯穿装置、交叉引用与 `PLAN.md` 一致；
4. 练习可操作（命令可直接粘贴运行）；
5. 图按批次 1 定案的形式（mermaid 或 ASCII）语法正确、可渲染；
6. `python scripts/repo_rules.py check` 通过（LF、链接、注释语言等）；
7. `mdbook build` 通过；本机暂无 mdbook/cargo 时，以静态检查替代：`SUMMARY.md`
   引用的章节文件全部存在、`book.toml` 语法正确、章节内相对链接有效（
   `repo_rules.py check` 已覆盖链接检查），环境就绪后补跑构建。
   （变更 2026-11-05，PART1 评审 R.1.2：本机无 mdbook/cargo）
8. 文风抽查：随机读三段，符合第 5 节规范（无禁用词、无模板化开头结尾、段落未被
   bullet 掏空）。

## 9. mdBook 工程方案与引用规范

- **布局**：`book/book.toml`（title/authors/language = zh）；源文件 `book/src/`，
  `book/src/SUMMARY.md` 控制目录；`PLAN.md` 与各评审文档**不进** SUMMARY；
- **输出**：`book/html/`，加入 `.gitignore`（构建产物不入库，与仓库既有约定一致）；
- **mermaid**：优先 `mdbook-mermaid` 插件；环境装不上则全稿退化为 ASCII 图
  （保 GitHub 原生渲染可读）。批次 1 落地时定案；
- **引用规范**：引用代码一律「符号名 + 相对文件路径」，**不写行号**（行号必烂）；
  引用真 asio 固定标注 1.38.2 的 `include/asio/...` 路径；涉及代码行数或篇幅估算时
  注明统计口径（是否含空行、签名行等）；
- **真 asio 源码核对方式**：本地克隆 `D:\Codes\asio`（chriskohlhoff/asio **1.38.2**，
  以 `include/asio/version.hpp` 的 `ASIO_VERSION 103802` 为准），全书对真 asio 的断言
  一律以 `D:\Codes\asio\include\asio\` 下的源码为真值来源；若克隆日后不在，退化为
  GitHub tag `asio-1-38-2` 在线核对或临时浅克隆到 `D:\Codes\_scratch\<任务目录>\`
  （工作区临时目录约定：用完即删，不进仓库、不进 book/）。

## 10. 评审协议

文件名状态机（`X` 为 `PLAN` 或 `PART<n>`）：

```
X-needreview.md   等待评审（评审员的回合）
X-reviewed.md     评审已追加，等待作者处理（作者的回合）
X-approved.md     落定（终态：共识达成，或轮次上限后作者裁决完毕）
book\STOP         总停车信号（全书完成或用户手动叫停，双方停止监控）
```

**运行模式**：双方均为持续监控——评审员每 10 秒扫描 `book\`（见
`REVIEWER-PROMPT.md`），出现 `-needreview` 即执行协议；作者由后台文件监视任务驱动，
出现 `-reviewed` 即处理并回应。协议执行完毕各自恢复监控，直到 `book\STOP` 出现
（全书完成时作者创建；用户可随时手动创建叫停协作）。

- **评审节**：评审员在文件末尾追加
  `## 评审 Round N（评审员 YYYY-MM-DD）`，条目格式 `R.N.x [阻塞|建议] 意见内容`
  （必须指向具体文件/小节/句子并说明理由），节末固定一行结论：
  `结论：待回应` 或 `结论：共识达成`（本轮无阻塞且作者此前回应均被接受）；
- **作者回应节**：作者在评审节后追加 `## 作者回应 Round N（作者 YYYY-MM-DD）`，
  逐条表态：采纳 / 部分采纳（说明改法）/ 拒绝（说明理由）。若有未决分歧，追加意见后
  将文件改回 `X-needreview.md`；若共识达成，更名为 `X-approved.md`；
- **轮次计数**：文件头部 `当前轮次：N/10` 由评审员在追加 Round N 时同步 +1；
- **轮次上限与裁决**：评审循环最多 10 轮。达到上限仍有未决阻塞时，**作者自主
  裁决**：在「作者回应 Round 10」中逐条记录处理决定（采纳已改 / 拒绝及理由），
  然后将文件更名为 `X-approved.md` 收尾——**全书完成前无人工介入**；
- **循环终止**：共识达成（提前收工），或达到 10 轮上限后按上一条由作者裁决收尾；
  除此之外不设人工介入环节；
- **红线**：评审只**追加**，不修改历史轮次；评审不改书稿正文、不改代码；作者回应后
  对书稿正文的修改在下一轮评审可见；
- **节奏**：每批一个评审循环；批内共识后作者提交该批内容，再开下一批。

## 11. 当前状态与下一步

- **状态**：批次 0（本计划）评审循环中；轮次以文件头部计数为准；
- **下一步**：本计划达成共识或轮次上限裁决后（更名为 `PLAN.md`）→ 开批次 1：
  mdBook 脚手架 + ch1、ch2 → 写 `PART1-needreview.md` → 评审循环 → 以此往复至
  全书完成。

## 评审 Round 1（评审员 2026-11-04）

- R.1.1 [阻塞] 第 7 节 ch2 的「全书锚点：handler 永远走队列」与代码不符。`scheduler::run_one_impl`（`src/scheduler.cpp`）在队列为空时会取出到期定时器并直接调用其 handler（`complete(error_code())`），而 `steady_timer::async_wait`（`src/steady_timer.cpp`）存进去的就是用户 handler，没有再包一层 post。因此「dispatch 是唯一就地例外」不成立：定时器到期是在 `run()` 循环内就地完成；socket 的 `complete_read` / `complete_write` / `complete_connect`（`src/ip_tcp.cpp`）才经 `scheduler_.post`。请把锚点改成可检验的两句话：I/O 完成一律经 post；定时器到期由 scheduler 在 run 循环内直接调用；共同点是 handler 不会在发起函数里内联执行。ch4 的 `run_one_impl` 精读与 ch6 的定时器完成都要按这个区分写，否则两章会各自建立相反的直觉。
- R.1.2 [阻塞] 第 7 节 ch4 的不变量②「work 归零即 stop」与 `scheduler::maybe_stop_if_idle`（`src/scheduler.cpp`）不符。实际停下条件是 `work_count_ == 0` 且队列为空、定时器堆为空、`reactor_.idle()` 为真；`has_work_locked` 也把待到期定时器和已注册描述符算作有工作。只有 work 计数归零、但仍有 pending timer 或 socket 时，`run()` 不会退出。请把不变量改成「队列、定时器、描述符三类来源都没有时才 stop」，或明确说明后两者不占 `work_count_` 但仍阻止 stop。
- R.1.3 [阻塞] 第 6 节称「唤醒的三次失守」每个都有仓内回归测试作证，但附录 C 写的是「两个回归测试」。仓内实际只有 `tests/asio/ip_tcp.cpp` 的 `tcp_multithread_wakeup_test` 与 `tests/asio/steady_timer.cpp` 的 `timer_multithread_wakeup_test`；post 一侧没有对应的 wakeup 测试——`tests/asio/io_context.cpp` 明确以 `OMITTED` 跳过了原始 asio 中「两个线程同时 run()」的用例。`scheduler::post`（`src/scheduler.cpp`）无条件调用 `reactor_.interrupt()`，也没有注释或提交记录把它记成一次失守。请二选一：补一个 post 跨线程唤醒回归测试，或把专栏改成「两次有测试的失守 + 一次设计说明」，并同步附录 C 的「两个」。
- R.1.4 [阻塞] 第 7 节 ch8 把 asio 对照指向 `detail/read_op.hpp`，但 1.38.2 的 `include/asio/detail/read_op.hpp` 不存在；`read_op` 类在 `include/asio/impl/read.hpp`，`base_from_completion_cond` 在 `include/asio/detail/base_from_completion_cond.hpp`。按验收标准第 5 条，对照表必须能回答「去 asio 哪里找」，这个路径会把读者带到不存在的文件；请改成实际路径，并核对 ch8 其他 asio 路径。
- R.1.5 [阻塞] 第 2 节把 README 的阅读顺序写成「13 步」、把差异内容写成「表」；当前 `README.md` 的 `Reading order` 只有 6 条编号步骤，差异在 `Deliberate limits` 的 bullet 列表里，没有表。ch14 的对照起点和附录 E 的底稿都建立在这两处描述上，需按实际内容改写，或明确计划以另一版 README 为准并先更新 README。
- R.1.6 [阻塞] 第 7 节 ch7 的不变量②「发起函数不做系统调用」被 `socket::start_connect`（`src/ip_tcp.cpp`）推翻：它直接调用 `detail::socket_ops::connect_socket`，并在立即成功或失败时调用 `complete_connect`（后者再 post）。`start_read_some` 也会经 `reactor_.start_read` 触发 `reactor::interrupt()` 的 `send`。请把不变量限定为「read/write/accept 的发起函数只登记兴趣、不做数据系统调用；connect 例外，发起时尝试一次非阻塞 connect」，否则 ch7 的两段式叙事会与代码矛盾。
- R.1.7 [建议] 第 1 节与 ch1 的「588 个头文件、16.4 万行」与本地 1.38.2 实测不符：`include/asio` 下 `.hpp` 为 600 个、169,063 行（16.9 万）；把 `.ipp` 也算上是 682 个、197,113 行。书名用「16 万」没问题，但正文既然给了精确数字，建议标注统计口径并按实测更新，或改成「约 600 个 .hpp、约 17 万行」。
- R.1.8 [建议] 第 7 节 ch4 说 `run_one_impl` 约 60 行；`src/scheduler.cpp` 里该函数实际约 76 行。可改成「约 80 行」，避免精读篇幅估算偏差。
- R.1.9 [建议] 第 8 节验收清单第 2 条写「无本地克隆时按第 8 节的核对方式」，但核对方式在第 9 节「真 asio 源码核对方式」。把交叉引用改为第 9 节。
- R.1.10 [建议] 第 4 节把六段骨架定为每章固定；ch12–ch14 没有 sasio 实现可精读（ch14 是阅读地图），按现文写会无法满足「代码精读 + sasio↔asio 对照表」。建议在第 4 节补一个 Part V 变体：精读 asio 符号、对照表改成「sasio 缺位 ↔ asio 实现 ↔ 去处」，并明确 ch14 豁免六段骨架。
- R.1.11 [建议] 第 9 节允许 mermaid 装不上时退化为 ASCII；第 8 节验收第 5 条却要求 mermaid 图可渲染。两者需要对齐：要么把验收改成「mermaid 或声明的 ASCII 图均可」，要么在批次 1 前把插件定为硬依赖。
- R.1.12 [建议] 「8 套平台后端」建议点名：`include/asio/detail` 下是 `epoll_reactor`、`kqueue_reactor`、`select_reactor`、`dev_poll_reactor`、`win_iocp_io_context`、`io_uring_service`，再加 `null_reactor`、`spin_reactor` 两个非平台实现；直接说「8 套平台后端」会把 null/spin 算进去。

结论：待回应

## 作者回应 Round 1（作者 2026-11-04）

- R.1.1 采纳。锚点已改为三句可检验表述：I/O 完成（read/write/accept/resolve）一律经
  `scheduler::post` 投递；到期定时器由 `run_one_impl` 在 run 循环内直接调用（不经队列）；
  `dispatch` 仅在「当前线程已在处理这个 context」时就地执行。ch2 锚点处已注明 ch4
  精读与 ch6 定时器完成都按这个区分展开。
- R.1.2 采纳。ch4 不变量②已改为 `has_work_locked()` 的四条件表述（队列空 + 定时器堆
  空 + 无已注册描述符才 stop），并注明定时器与描述符不占 `work_count_` 但同样阻止
  stop。
- R.1.3 采纳二选一中的**后者**：post 的唤醒从第一版起就正确（`scheduler::post` 无条件
  `reactor_.interrupt()`），它不是失守——补一个「回归测试」反而会把天生的正确伪装成
  曾经缺失。专栏改为「唤醒的两次失守 + 一次天生的正确」，两次失守各配现有回归测试
  （`tcp_multithread_wakeup_test`、`timer_multithread_wakeup_test`）；附录 C 的「两个」
  与实际一致，未动。
- R.1.4 采纳。本机 1.38.2 实测确认：`detail/read_op.hpp` 与 `detail/write_op.hpp` 均不
  存在，`class read_op` / `write_op` 定义在 `impl/read.hpp` 与 `impl/write.hpp`。
  ch8 对照已改为实际路径；`base_from_completion_cond.hpp` 路径核对无误、保留。
- R.1.5 采纳。§2 两行已按当前 `README.md` 实测改写（`Reading order` 6 步、
  `Deliberate limits` 列表），并注明 6 步与 ch14「6 周计划」的映射关系。
- R.1.6 采纳。ch7 不变量②已限定为 read/write/accept 只登记兴趣；connect 的非阻塞
  尝试（`start_connect` 直接调 `connect_socket`）与 interrupter 控制面 send 均已注明
  为例外。
- R.1.7 采纳。ch1 改为实测口径：`include/asio` 下 `.hpp` 600 个 / 169,063 行
  （含 `.ipp` 约 19.7 万行），统计口径随数字标注；书名保留「16 万行」约数。
- R.1.8 采纳。ch4 已改「约 80 行，实测 83」。
- R.1.9 采纳。验收第 2 条交叉引用已改为第 9 节。
- R.1.10 采纳。§4 已加 Part V 变体：ch12–13 的精读对象换成真 asio 符号与片段、
  对照表改为「sasio 缺位 ↔ asio 实现 ↔ 去处」三栏；ch14 豁免六段骨架，改用
  「分层导览 → 6 周计划 → 自测问题」结构。
- R.1.11 采纳。验收第 5 条已改为「图按批次 1 定案的形式（mermaid 或 ASCII）语法
  正确、可渲染」。
- R.1.12 采纳。ch1 与 ch14 已点名 6 套平台 I/O 后端（epoll、kqueue、select、dev_poll、
  IOCP、io_uring），并注明 `null_reactor`、`spin_reactor` 两个非平台桩（本机
  `detail/` 目录实测与评审员列举一致）。

全部 12 条均已落实到正文，本轮无未决分歧。按协议，修改需评审员复核确认后方可
落定，文件改回 `-needreview`。

## 评审 Round 2（评审员 2026-11-04）

- R.2.1 [阻塞] 第 7 节 ch4 的不变量②仍少一个条件：正文写「队列为空、定时器堆为空且无已注册描述符时 scheduler 自行 stop」，但 `scheduler::maybe_stop_if_idle`（`src/scheduler.cpp`）还要求 `work_count_ == 0`。持有 `io_context::work` 时队列、定时器、描述符都可能为空，而 `work_count_` 仍大于 0，scheduler 不会 stop；这正是本章随后要讲的 work guard 语义。请把 `work_count_ == 0` 补进条件（或明确「四个条件」是 work_count、队列、定时器、描述符），否则 ch4 的不变量与 `io_context::work` 小节互相矛盾。
- R.2.2 [阻塞] R.1.5 只落实了第 2 节；附录 E 仍写「README 差异表扩展为逐章差异地图」。当前 `README.md` 没有差异表，只有 `Deliberate limits` 列表。请把附录 E 改成「README 的 `Deliberate limits` 列表扩展为逐章差异地图」，与第 2 节保持一致。
- R.2.3 [建议] 第 7 节 ch2 的 I/O 完成清单写「read/write/accept/resolve」，漏了 connect；`socket::complete_connect`（`src/ip_tcp.cpp`）同样经 `scheduler_.post`。把它补进清单，或改成「所有 socket/acceptor/resolver 完成」，避免读者以为 connect 是例外。
- R.2.4 [建议] 第 1 节说英文版翻译「见第 8 节」，但第 8 节是批次与验收，没有翻译批次；可在第 8 节批次表加一行「6 英文版翻译（全书完成后）」，或把引用指向实际条目。
- R.2.5 [建议] 第 11 节状态仍写「当前轮次：0/10」，与头部计数（本轮后 2/10）不一致；建议改为与头部一致，或删掉这处重复计数。
- R.2.6 [建议] R.1.8 的计数口径：我上轮按非空行数报 76，实际函数跨度是 83 行（含空行），作者的「实测 83」正确；建议后续篇幅估算统一注明是否含空行，免得再出现同类偏差。

结论：待回应

## 作者回应 Round 2（作者 2026-11-04）

- R.2.1 采纳。评审员指出的自相矛盾属实：Round 1 的改法列了三个条件、括号里却写
  「四个条件」，且漏掉的正是 `work_count_ == 0`——work guard 的语义恰恰在这里。
  ch4 不变量②已改为四条件全空才 stop，并补上「持有 `io_context::work` 时即使后三者
  全空也不停」，与本章 work guard 小节对齐。
- R.2.2 采纳。附录 E 已改为「README 的 `Deliberate limits` 列表扩展为逐章差异地图」，
  与 §2 一致。
- R.2.3 采纳。ch2 锚点的 I/O 清单已补 connect（`socket::complete_connect` 同样经
  `scheduler_.post`）。
- R.2.4 采纳。§8 批次表已加批次 6「英文版翻译（全书完成后）」，§1 的引用落到了
  实际条目。
- R.2.5 采纳。§11 的重复计数已删，轮次以文件头部为准。
- R.2.6 采纳。§9 引用规范已加「涉及代码行数或篇幅估算时注明统计口径」。

全部 6 条均已落实，本轮无未决分歧。文件改回 `-needreview`，请评审员复核；若本轮
复核通过并判「共识达成」，作者即将文件落定为 `PLAN-approved.md` 并启动批次 1。

## 评审 Round 3（评审员 2026-11-04）

- R.3.1 [建议] 复核通过，无新增阻塞。仅提醒一处后续写法：第 7 节 ch2 的 `dispatch` 就地条件描述的是 `scheduler::dispatch`（按 context）；ch10 的 `strand::dispatch` 按「当前 strand」就地执行，两者不是同一个判断。ch10 首次讲 strand 时点明这层区别即可，不必改计划。

结论：共识达成
