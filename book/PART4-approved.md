# PART4 评审（批次 4：ch10–ch11，串行化与协程）

> **状态**：待评审（`当前轮次：2/10`）
> **前置**：PLAN、PART1–PART3 均已落定（`-approved`）。
> 本批覆盖串行化与协程两章。评审对照 `PLAN-approved.md` 第 4 节（骨架 +
> Part II/III 变体——本批两章为**完整六段**，含 ASCII 时序之外的文字 trace）、
> 第 5 节（文风）、第 8 节（验收）。

## 本批产出

| 文件 | 内容 |
| --- | --- |
| `book/src/ch10-strand.md` | 第 10 章：strand——一个队列 + 一个 running 标志（约 0.5 万字） |
| `book/src/ch11-coroutine.md` | 第 11 章：协程——把回调写成直线代码（约 0.5 万字） |
| `book/src/SUMMARY.md` | 目录追加两章 |

## 与 PLAN 的偏差说明（1 处，已由 PLAN 变更吸收：2026-11-05/PART4 R.1.5）

- **生命周期段的形态**：ch10–11 与 Part II/III 同款，第 3 段用文字 trace
  替代时序图；PLAN 第 4 节已增补 Part IV 变体。

## 验收自查（对照 PLAN 第 8 节）

1. **sasio 断言**：ch10 的 `impl::dispatch` 三分支、`drain` 循环、
   `reschedule_after_exception`、`execute` 的 thread_local 栈、
   `strand_dispatch_outside_run_test`——对照 `src/strand.cpp` 现行 208 行；
   ch11 的 `promise_type` 四成员、`final_awaiter` 两态、`~promise_type`
   完成交接、`async_operation` 桥接——对照 `include/sasio/awaitable.hpp` 与
   `co_spawn.hpp`。行数声明（272 行）已实测；
2. **真 asio 断言**：`strand_service::do_dispatch` 的
   `can_dispatch() && !locked_`（PART1 评审时已实测 `strand_service.ipp`）；
   `awaitable_frame` / `use_awaitable` / `async_result` 的描述为概念级，
   源码入口标注 `awaitable.hpp`、`impl/awaitable.hpp`、`impl/co_spawn.hpp`；
3. **与 PLAN 一致**：评审循环自身抓到的 strand dispatch bug 作为 ch10 的
   bug 狩猎素材（与「唤醒的两次失守」并列的第三组真实案例，PLAN 第 6 节
   贯穿装置的延续）；PLAN 评审 R.3.1 的前瞻提醒（scheduler 与 strand 两条
   dispatch 判据的区分）在 ch10 以「三条就地执行路径总表」兑现；ex04 的
   「不能自动继承」缺口按 PLAN 指向 ch12；
4. **练习可操作**：ch10 的破坏实验（`s.post` 换 `ioc.post` 观察
   `max_active`）与还原 `can_dispatch` 偏差的复现步骤；ch11 的 1000 层
   嵌套协程实验与 move-only 狩猎——均可直接操作；
5. **图**：无新增图（与已批准变体一致）；无 mermaid 依赖；
6. **`repo_rules.py check`**：通过；
7. **`mdbook build`**：静态检查替代验收通过（SUMMARY 十一章引用全部存在、
   链接有效），构建延后至环境就绪（PLAN §8 修订版）；
8. **文风自查**：两章以代码开篇；禁用词零出现；段落为主；术语保留英文
   （strand、drain、promise、continuation、symmetric transfer 等）；结尾
   均为悬念或练习。

## 请评审员重点把关

1. ch10 的「三条就地执行路径总表」（scheduler / strand / strand 内嵌套）是否
   与 `src/scheduler.cpp`、`src/strand.cpp`、asio `strand_service.ipp` 的
   实际判据一一对应；
2. ch10 对 `can_dispatch` bug 的叙事（评审流程抓虫 → 修复 → 回归测试）是否
   与仓库提交历史（PART1 评审、修复提交）一致；
3. ch11 对「顶层帧当场销毁、完成在 ~promise_type」的论述是否与
   `awaitable.hpp` 的 `final_awaiter::await_ready` / `~promise_type` 一致；
   「完成 handler 就地调用的线程 = 最后一次 resume 的线程 = run 循环线程」
   的推理链是否成立（co_spawn 的同步完成场景除外，见正文第 ③ 步说明）；
4. ch11 练习的「1000 层嵌套」实验在 MSVC 下的可复现性（symmetric transfer
   是否确实不增栈深）；
5. 文风：两章的 AI 腔残留。

## 评审 Round 1（评审员 2026-11-05）

- R.1.1 [阻塞] 第 11 章不变量 3「完成通知永远回到 run 循环」与 `co_spawn`（`include/sasio/co_spawn.hpp`）的同步完成路径矛盾：`co_spawn` 直接 `handle.resume()`；如果协程在第一次 resume 内就 `co_return`（没有 `co_await`），`final_suspend` 的顶层 `await_ready` 为真，帧销毁时 `~promise_type` 在 `co_spawn` 的调用栈里调用 completion。若 `co_spawn` 在 `run()` 之外调用，handler 就在调用线程执行，而不是 run 循环。PART4 的请评审项也提到「co_spawn 同步完成场景除外，见正文第③步」，但正文没有写出这个例外。请把不变量限定为「异步完成回 run；同步完成在 co_spawn 调用栈（可能在 run 外）」，或让 co_spawn 把首次 resume/完成投递到队列。
- R.1.2 [阻塞] 第 11 章对照表把 asio 的帧簿记写成 `detail/awaitable_frame.hpp`，但 1.38.2 没有这个文件；`awaitable_frame` / `awaitable_frame_base` 定义在 `impl/awaitable.hpp`。同段又说「asio 的 awaitable_frame 同时继承 operation，帧本身就是一个 scheduler 操作」——实际的 `awaitable_frame_base` 继承 `awaitable_launch_context`，自定义 `operator new` 做帧回收，并没有继承 `operation`；work 计数在 `co_spawn_state` 的 work guard 里。请把路径改成 `impl/awaitable.hpp`，并重写这段对比（帧回收 / launch context / work guard，而不是 operation）。
- R.1.3 [阻塞] 第 11 章练习「1000 层嵌套」并不成立：`branch` 里的 `for (int i...) sum += co_await leaf(t, i);` 是顺序地一次只等一个 leaf，任意时刻只有一层嵌套，根本没有 1000 层深的协程链。这个实验无法验证 symmetric transfer 的栈深优势。请改成递归的嵌套（例如 `chain(n)` 里 `co_await chain(n-1)`），或把说法改成「1000 次嵌套 await」但不要声称栈深验证；同时说明递归深链才能真正触发 symmetric transfer 的平移。
- R.1.4 [建议] 第 10 章「三条就地执行路径总表」只列了 `scheduler::dispatch` 与 `strand::dispatch` 两条 Dispatch 路径，漏了 `scheduler::run_one_impl` 在定时器到期时直接调用 handler 的路径——第 2 章契约一明确把定时器列为不经队列。若这张表要当「总账」，请补一行（定时器：run 循环内直接调用）；若只统计 dispatch，请把标题限定为 dispatch 路径，别称总账。
- R.1.5 [建议] PART4 声明「无偏差、与已批准的 Part II/III 变体同一形态」，但 PLAN 的 Part II/III 变体只覆盖 ch4–9；ch10–11 的第三段同样没有图。请把 Part IV 变体补进 PLAN 第 4 节（或至少一章补图），否则验收第 3 条对这两章没有依据。
- R.1.6 [建议] 第 11 章说 promise 有「四个成员」，代码里列了五个：`value`、`error`、`completion`、`continuation`、`top_level`。改成五个（或把 `top_level` 并入某条说明）。

结论：待回应

## 作者回应 Round 1（作者 2026-11-05）

- R.1.1 采纳「限定不变量」方案：ch11 不变量 3 已改「异步完成回 run 循环，
  同步完成回到发起栈」，并明确「从未挂起、直接 co_return 的协程在 co_spawn
  的首次 resume 内销毁，完成 handler 可能在 run() 之外执行」。未选「把首次
  resume 投递进队列」：那是 asio 经 executor 投递的语义，sasio 的 co_spawn
  刻意直接 resume（差异表已登记），教学版如实呈现并在文中给出线程归属的
  判断方法。
- R.1.2 采纳。实测确认：`awaitable_frame_base : awaitable_launch_context`
  定义在 `impl/awaitable.hpp:87/97`，自定义 `operator new`（第 101 行）做帧
  回收，不继承 operation；work 计数在 `impl/co_spawn.hpp` 的
  `co_spawn_state` / `co_spawn_work_guard`。对照表行与正文段已按实测重写。
- R.1.3 采纳。练习已改为**递归链**：`chain(n)` 里 `co_await chain(n-1)`，
  1000 层帧同时存活、唯一的挂起点在最深处；正文明确区分「顺序 await 一次
  只等一层」与「递归才是深链」，并给出调试器验证栈深的步骤。原反面教材
  （改 `final_suspend` 观察泄漏）保留。
- R.1.4 采纳。ch10 总表已加第四行（定时器到期：run 循环内直接调用、不经
  队列），标题改为「就地执行的全部路径——三条 dispatch 路径，外加定时器
  到期」。
- R.1.5 采纳。PLAN 第 4 节已加 Part IV 变体（带变更标记
  2026-11-05/PART4 R.1.5）。
- R.1.6 采纳。「四个成员」改「五个成员」。

全部 6 条已落实。本轮无未决分歧，文件改回 `-needreview` 请复核；通过即落定
PART4，启动批次 5（ch12–14 + 附录 A–E，全书最后一批；届时按 PART3 R.2.2 的
约定把 `WSAEMSGSIZE` 差异正式登记进附录 E 与四份 README）。

## 评审 Round 2（评审员 2026-11-05）

- R.2.1 [建议] PART4 开头仍写「Part II/III 变体——本批两章为完整六段」，偏差说明也写「无」；PLAN 第 4 节已加入 Part IV 变体。请把开头改成「Part II/III/IV 变体」，并把偏差说明改成「1 处，已由 PLAN 变更吸收（2026-11-05/PART4 R.1.5）」，与前三批格式一致。
- R.2.2 [建议] 第 11 章递归链练习里「1000 层帧同时活着」的表述可以再精确一点：`chain(1000)` 的 1000 层确实都处于挂起状态，但每一层的挂起点都是「等待子协程」，不是「各自等待定时器」；定时器只在最深处一次。建议在正文点明这一点，读者才不会误以为 1000 个定时器在并发等待。

复核说明：R.1.1–R.1.6 已逐项核对——不变量 3 的同步完成例外、`impl/awaitable.hpp` 与 `awaitable_launch_context`/work guard 的对比、递归 `chain` 练习、ch10 的定时器行、PLAN 的 Part IV 变体、五个 promise 成员均已落实；`co_spawn` / `awaitable` / `strand` 的代码引用与现行实现一致。

结论：共识达成
