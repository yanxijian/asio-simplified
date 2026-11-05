# PART2 评审（批次 2：ch3–ch6，引擎四部曲）

> **状态**：待评审（`当前轮次：2/10`）
> **前置**：`PLAN-approved.md`（批次 0）、`PART1-approved.md`（批次 1）均已落定。
> 本批覆盖引擎四部曲：ch3 service 注册表、ch4 scheduler、ch5 reactor、ch6 定时器。
> 评审时对照 `PLAN-approved.md` 第 4 节（骨架 + Part 0/Part V 变体）、第 5 节
> （文风规范）、第 8 节（验收清单），并注意 PLAN 第 4 节 Part 0 变体只适用于 ch1。

## 本批产出

| 文件 | 内容 |
| --- | --- |
| `book/src/ch03-services.md` | 第 3 章：io_context 为什么这么薄——service 注册表（约 0.45 万字） |
| `book/src/ch04-scheduler.md` | 第 4 章：scheduler——handler 队列与 work 计数（约 0.5 万字） |
| `book/src/ch05-reactor.md` | 第 5 章：reactor——就绪、唤醒与描述符表，含案例专栏（约 0.5 万字） |
| `book/src/ch06-timer.md` | 第 6 章：时间——timer_queue 与 steady_timer，含 pop_at bug 故事（约 0.5 万字） |
| `book/src/SUMMARY.md` | 目录追加四章 |

## 与 PLAN 的偏差说明（1 处，已由 PLAN 变更吸收：2026-11-05/PART2 R.1.9）

- **生命周期追踪段的形态**：ch4–ch6 的第 3 段没有画时序图，改用「文字 trace +
  代码骨架」。理由：这三章的「一次操作」就是 `run_one_impl` 循环本身与
  `run_for` 的四段骨架——第 2 章已经画过一次完整的时序图，第 4 章再画同一循环
  的图会是重复；ch4 用「work 的一生」三种走法做 trace，ch5 用「一次唤醒的一生」
  五步，ch6 用「等待 / 取消 / 析构」三条生命线。若评审员认为至少一章应有图，
  作者可补（ASCII，与批次 1 定案一致）。

## 验收自查（对照 PLAN 第 8 节）

1. **sasio 断言**：ch4 的 `run_one_impl` 83 行、`handler_work` 记账、
   `has_work_locked` 四条件；ch5 的 `start_read` first 判定、`interrupt`
   exchange 去重、`run_for` 四段骨架（锁内抄快照/锁外 poll/移出再执行/锁外
   跑回调）；ch6 的 `pop_at` 修复、`add` 返回 earliest、`cancel` 经 post——
   全部对照本仓现行代码撰写，代码块为真实代码或注明「骨架」的删减版；
2. **真 asio 断言**：`select_reactor::start_unready_op` 的
   `if (first) interrupter_.interrupt()`、`select_reactor::schedule_timer` 的
   `if (earliest)`、`strand_service::do_dispatch` 的
   `can_dispatch() && !locked_`、`epoll_reactor::update_timeout` 的
   `interrupt()`、`scheduler.ipp` 的 `outstanding_work_ == 0`——五处均在本机
   1.38.2 源码逐一核实过行号与内容；
3. **与 PLAN 一致**：贯穿装置 2（唤醒的两次失守 + 一次天生的正确）主体落在
   ch5、ch6 回收 earliest；贯穿装置 3（pop_at bug、CRT 防线）在 ch6 展开前者；
   引擎契约三句（ch2）在 ch4 精读、ch5 两次失守处被反复引用；「6 周计划」的
   章节映射暂不涉及（ch14）；
4. **练习可操作**：ch4 work 泄漏程序、ch5 注释 `if (first) interrupt()` 复现
   失守、ch6 还原旧版 `pop_at` + 双定时器用例复现越界断言——三个都是「改
   一处、跑一次、看现象」的操作，且都要求改回（练习性改动，不提交）；
5. **图**：ch4–ch6 无图（见偏差说明）；无 mermaid 依赖；
6. **`repo_rules.py check`**：通过；
7. **`mdbook build`**：按 PLAN 修订版以静态检查替代（SUMMARY 引用的六个文件
   全部存在、相对链接有效），构建延后至环境就绪；
8. **文风自查**：ch3–ch6 均以代码或上一章遗留问题开篇；正文段落为主；
   禁用词零出现；术语保留英文；每章结尾为悬念或练习。

## 请评审员重点把关

1. ch4 对 `run_one_impl` 的逐段讲解是否与 `src/scheduler.cpp` 现行 83 行
   （含第 3 段的 `polled` 标志、`maybe_stop_if_idle` 时机）逐句对得上；
2. ch5 案例专栏的历史叙事是否与仓库提交历史一致（两次失守分别是 chapter 2 /
   chapter 1 修复，post 从第一版就正确）；
3. ch6 对 `pop_at` bug 的机制还原（脏索引 → 析构时 remove 越界）是否与
   `timer_queue.cpp` 注释、PLAN 第 6 节（原第 6 节「贯穿装置 3」）一致；
4. 偏差说明（生命周期段无图）是否成立，或要求补图；
5. 文风：四章的 AI 腔残留。

## 评审 Round 1（评审员 2026-11-05）

- R.1.1 [阻塞] 第 3 章的对照表与解释把 asio 的 service 说成「可以按 I/O 对象挂（每个 socket 一份）」「可以被多处引用计数」，这与 1.38.2 不符。`basic_io_object`（`include/asio/basic_io_object.hpp`）从 `use_service<IoObjectService>(io_context)` 取得的是**每个 execution_context 一份**的 service 引用，每个 I/O 对象只有自己的 `implementation_`；`detail/service_registry` 也没有引用计数服务，它维护 `first_service_` 链表，另提供 `add_service` / `has_service` / `notify_fork` 与 `shutdown` / `destroy` 分离。因此 ch3 的「服务可以按 I/O 对象挂」「引用计数服务」两处断言和对照表的 `asio 独有` 行都不成立。请改成真实差异（key/type_info、add_service、notify_fork、shutdown/destroy 分离），否则读者会带着错误模型去读 `service_registry`。
- R.1.2 [建议] 第 3 章「五行各有一个理由」下面只列了 ①–④ 四条；代码里也只有四个编号注释。把「五」改成「四」，或补上第五个理由（例如 `order_` 的作用单独编号）。
- R.1.3 [建议] 第 3 章「销毁按创建的逆序进行」与 `execution_context::shutdown_services` 的实际语义有偏差：逆序的是 `shutdown()`（`order_` 反向遍历），之后 `services_.clear()` 按容器自身的顺序销毁对象。建议写成「shutdown 按创建逆序，随后统一销毁」，并把「reactor 先被拆除」改成「reactor 先 shutdown」，避免把 shutdown 与析构混为一谈。
- R.1.4 [建议] 第 4 章「计数归零本身不直接 stop……在下一圈成立」不够准确：`scheduler::on_work_finished` 在递减后立即调用 `maybe_stop_if_idle()`；如果四条件已空，`stopped_` 当场置位（随后由 run 循环观察到）。建议改成「归零后立即检查四条件；从 handler 返回时可能已经 stop」，与 `src/scheduler.cpp` 对齐。
- R.1.5 [建议] 第 4 章 work 泄漏练习的最后一句「把 guard 换成构造后立刻 reset()，验证 run() 立刻返回 0」不严谨：前面已经 `post` 了一个 handler，reset 后队列里仍有它，`run()` 会执行它并返回 1，不是 0。要看到 0，应展示一个不 post 的第二个 context；建议把这段改成完整的两段代码，避免读者照做后困惑。
- R.1.6 [建议] 第 5 章对照表的列名 `asio（POSIX 脸）` 下面写了 `select() / WSAPoll`；WSAPoll 是 Windows 的 select 等价物，不属于 POSIX。建议把列名改成「select 脸（POSIX select / Windows WSAPoll）」，或把 WSAPoll 单列，避免平台归属错误。
- R.1.7 [建议] 第 6 章结尾「唤醒路径的三次修复也全部到位」与第 5 章及 PLAN 的「两次失守 + 一次天生的正确」冲突；post 不是修复。建议改成「两条修复 + 一条天生的正确」或「三条唤醒路径全部到位」。
- R.1.8 [建议] 第 6 章 bug 狩猎说「remove 失败只有两种原因——索引是 npos/越界/身份不符」，实际列了三种条件。改成「三种」或明确合并 npos 与越界为「索引不可用」再加身份不符。
- R.1.9 [建议] 关于本批「生命周期段无图」的偏差：文字 trace 已经覆盖了每章的一次完整时序（work 的三种走法、一次唤醒的五步、等待/取消/析构三条生命线），可以接受，不必强补图；但请把「Part II 变体：第 3 段用文字 trace 替代 mermaid」补进 PLAN 第 4 节并带变更标记，否则验收第 3 条仍按「六段骨架含图」判 ch4–ch6 不合格。
- R.1.10 [建议] 第 5 章练习命令 `build\test_ip_tcp.exe` 只在 Ninja 下成立；第 1 章说过默认 VS 生成器的可执行文件在 `build\Debug\`。建议像 ch2 那样补一句默认生成器的路径，保证「命令可直接粘贴」在 fallback 下成立。

结论：待回应

## 作者回应 Round 1（作者 2026-11-05）

- R.1.1 采纳，并向评审员的核实致谢——本机实测确认：`basic_io_object.hpp:116`
  为 `service_(asio::use_service<IoObjectService>(io_context))`，service 每个(context,
  类型) 一份、I/O 对象只持 `implementation_`；`service_registry.hpp` 只有
  `first_service_` 链表与 `add_service` / `has_service` / `notify_fork`，无引用计数
  服务。ch3 的两处错误断言与对照表 `asio 独有` 行已改为真实差异（链表、
  add/has、notify_fork、shutdown/destroy 分离、per-context 共享 + per-object
  implementation）。
- R.1.2 采纳。「五行」改「四处」。
- R.1.3 采纳。不变量 2 与递归链段已改为「shutdown 按创建逆序，随后统一销毁」，
  「reactor 先被拆除」改「reactor 先 shutdown」，`interrupter` 例句同步修正。
- R.1.4 采纳。ch4 已改「`on_work_finished()` 立即调用 `maybe_stop_if_idle()`，
  四条件若已空 `stopped_` 当场置位」，并删除「推迟到下一圈」的错误对比。
- R.1.5 采纳。练习改为两段完整代码：第一个 context 泄漏挂住；第二个 context
  不 post、guard 立即 `reset()`，`run()` 返回 0——避免照做后返回 1 的困惑。
- R.1.6 采纳。对照表列名改「asio（select 脸）」，`WSAPoll` 在格内标注
  「无 IOCP 的 Windows 上」。
- R.1.7 采纳。ch6 结尾改为「三条唤醒路径全部到位：两条失守被测试抓住并修复，
  一条从第一版就正确」。
- R.1.8 采纳。「两种原因」改「三种原因」，并列出三种。
- R.1.9 采纳。PLAN 第 4 节已加 Part II 变体（文字 trace 替代时序图，带变更
  标记 2026-11-05/PART2 R.1.9）；偏差声明在 PART2 内保留。
- R.1.10 采纳。ch5 练习已补默认 VS 生成器路径说明。

全部 10 条已落实。本轮无未决分歧，文件改回 `-needreview` 请复核；通过即落定
PART2，启动批次 3（ch7–ch9：socket 两段式、组合操作、UDP 与 resolver）。

## 评审 Round 2（评审员 2026-11-05）

- R.2.1 [建议] 第 3 章对照表的行名仍写「逆序销毁」，而正文已改成「shutdown 按创建逆序，随后统一销毁」。建议把行名改成「shutdown 顺序」或「shutdown / 销毁」，免得读者把 shutdown 与析构混为一谈。
- R.2.2 [建议] PART2 的偏差说明还写「1 处，请评审员裁定是否成立」，但 PLAN 第 4 节已加入 Part II 变体；建议把它改成「1 处，已由 PLAN 变更吸收（2026-11-05/PART2 R.1.9）」，与 PART1 偏差记录的格式一致。

复核说明：R.1.1–R.1.10 已逐项核对——ch3 的 service 归属与对照表已按 1.38.2 修正，ch4 的 `on_work_finished` 与 work guard 练习已改，ch5 的列名与练习路径已补，ch6 的「三次修复」「两种原因」已统一；PLAN 的 Part II 变体已带变更标记。代码块与 `src/scheduler.cpp` / `src/reactor.cpp` / `src/timer_queue.cpp` 的现行实现一致。

结论：共识达成
