# PART1 评审（批次 1：mdBook 脚手架 + ch1、ch2）

> **状态**：待评审（`当前轮次：3/10`）
> **前置**：`PLAN-approved.md`（批次 0 已于 Round 3 共识落定）。
> 本文件覆盖批次 1 全部产出。评审时请对照 `PLAN.md`（即 `PLAN-approved.md`）的
> 骨架模板（第 4 节）、文风规范（第 5 节）与验收清单（第 8 节）。

## 本批产出

| 文件 | 内容 |
| --- | --- |
| `book/book.toml` | mdBook 配置（title/authors/language=zh，源目录 `src/`） |
| `book/src/SUMMARY.md` | 目录（当前仅 ch1、ch2，后续批次追加） |
| `book/src/ch01-why.md` | 第 1 章：为什么先读 4200 行的版本（约 0.35 万字） |
| `book/src/ch02-model.md` | 第 2 章：Reactor 与 Proactor，一次异步操作的完整生命周期（约 0.45 万字） |
| `.gitignore` | 追加 `book/html/`（mdBook 输出目录） |

## 与 PLAN 的偏差记录（2 处，均已由 PLAN 变更吸收，见作者回应 Round 1/2）

- **图的形式**：PLAN 第 9 节允许「mdbook-mermaid 装不上则全稿退化为 ASCII 图」。
  本机无 `mdbook` 与 `cargo`，已按预案**定案为 ASCII 图**（ch2 的时序图即 ASCII）。
  mdBook 脚手架照常建好，将来装上 `mdbook` 即可出 HTML，源文件无需改动。
- **Part 0 骨架变体**：ch1 以「全书路线图」顶替「生命周期追踪」段。PLAN 第 4 节
  已增补 Part 0 变体（PART1 评审 R.1.3，作者采纳），本条由偏差转为已批准的变体。

## 验收自查（对照 PLAN 第 8 节）

1. **sasio 断言**：ch1 的 `ex01_post` 程序体、post/run/restart 行为、9 个测试数量
   与耗时；ch2 的 `run_one_impl` 83 行、三来源循环形状、发起/就绪/投递三段链路——
   均与本仓代码一致；
2. **真 asio 断言**：600 个 `.hpp` / 169,063 行（1.38.2 实测）、6 套平台后端 +
   null/spin 桩、`win_iocp_io_context.hpp` / `epoll_reactor.hpp` /
   `select_reactor.hpp` 路径——已逐一核实；
3. **与 PLAN 一致**：引擎契约三句即 PLAN 第 7 节 ch2 锚点原文；三步法、六段骨架
   （Part 0 的 ch1 按「问题/方法/路线/精读/对照/练习」套用，其中「路线」对应
   生命周期段的位置）、贯穿装置 1（echo server，ch2 练习即用 ex03）均已对齐；
4. **练习可操作**：ch1 的 cmake/ctest 命令、ch2 的 `ex03_echo_server 5555` +
   telnet 步骤，均可直接粘贴运行；
5. **图**：ASCII 定案（见偏差记录），ch2 时序图为手绘 ASCII，源文件在 GitHub 与
   mdBook 下均可读；
6. **`repo_rules.py check`**：通过（本批提交前已运行）；
7. **`mdbook build`**：静态检查替代验收通过（PLAN 第 8 节第 7 条已修订——本机
   无 mdbook/cargo，以「SUMMARY 引用存在、book.toml 语法、链接有效」替代，构建
   延后至环境就绪）；
8. **文风自查**：两章均以代码开篇；无禁用词（深入/优雅/强大/全面等）；正文
   段落为主，列表仅用于并列项；术语保留英文（handler、work、reactor、proactor、
   contract 等）；两章结尾均为悬念或练习，无「本章小结」式收尾。

## 请评审员重点把关

1. ch2 引擎契约三句与 `scheduler.cpp` / `ip_tcp.cpp` / `steady_timer.cpp` 的实际
   行为是否句句可检验（尤其「I/O 完成含 connect 经 post」与「定时器不经队列」）；
2. ch1 对 asio 规模的口径（600/169,063、6+2 后端）与 ch2 对 sasio/asio 对照表的
   对应关系；
3. ch1 的 Part 0 骨架套用是否成立（PLAN 未给 Part 0 变体，本批以「路线图」段
   顶替「生命周期追踪」段，如不成立请指出）；
4. 文风：两章是否存在 AI 腔残留。

## 评审 Round 1（评审员 2026-11-04）

- R.1.1 [阻塞] 第 2 章「引擎契约」的契约二「没有 run()，就没有任何 handler 会执行」与 `strand::impl::dispatch`（`src/strand.cpp`）不符：当 `current_strand_impl != this` 且 strand 空闲时，`dispatch` 取得 strand、把 `running` 置真，然后直接 `execute(std::move(handler))`；它没有 `scheduler::post`，也不检查调用线程是否在 `run()` 里。因此 `strand s(ctx); s.dispatch(h);` 在没有 `ctx.run()` 时就会在调用线程执行 h。契约一又说 `dispatch` 只在「当前线程已在处理这个 context」时就地执行，但 strand 的判断是「当前 strand」，不是 context/run。请二选一：修 `strand::impl::dispatch`，让 strand 外的 `dispatch` 走 `scheduler.post`（更接近 asio，并补测试）；或把两条契约限定为 scheduler 投递路径，并在 ch10 明说 `strand::dispatch` 的例外。现状会让 ch2 的地基与 ch10 冲突。
- R.1.2 [阻塞] 批次 1 验收第 7 条（`mdbook build` 通过）未执行，PART1 的偏差记录也确认本机无 mdbook/cargo。PLAN 第 8 节写「批次 1 起：mdbook build 通过」，未满足就不能按第 8 节宣告本批验收通过。请补跑，或在 PLAN 第 8 节写明环境无 mdbook 时的替代验收（例如静态检查 `book.toml`/`SUMMARY.md`/链接，构建延到环境就绪的批次），并同步 PART1；这属于计划变更，需要留下记录。
- R.1.3 [建议] 第 1 章没有按 PLAN 第 4 节的六段骨架写：它有「方法/路线/第一段代码/对照/练习」，没有「设计（≤3 条不变量）」和「生命周期追踪」，而正文又说「每章固定六段」。作为 Part 0 的引子，用路线图替代生命周期是合理的；建议把「Part 0 变体」补进 PLAN 第 4 节（与 Part V 变体并列），并把这条列入 PART1 的偏差记录，否则验收第 3 条对 ch1 无法自洽。
- R.1.4 [建议] 第 1 章「scheduler 一样用 work 计数决定停止」不完整：`scheduler::maybe_stop_if_idle`（`src/scheduler.cpp`）还要求队列、定时器堆、描述符表都空；只有 work 计数归零、但仍有 pending timer/socket 时不会停。建议改成「work 计数 + 三类待办来源」，与第 4 章一致。
- R.1.5 [建议] 第 1 章「先看 sasio 的完整程序——`examples/ex01_post.cpp` 的正文」下面的代码块只有 `io_context/post/run` 几行，不是完整程序；同段又说「20 行代码」，而 `examples/ex01_post.cpp` 实际 31 行。建议改成「核心几行」或贴出完整文件，并把「20 行」改成实际行数。
- R.1.6 [建议] 第 2 章「精读：引擎的循环骨架」的代码块里，注释和条件都是中文（`// 有人喊停`、`if (队里有 handler)` 等）。PLAN 第 1、5 节要求代码与代码块注释英文；建议改成英文伪代码（`if (!queue.empty())`、`// stop requested`），或把这段改成正文/ASCII 流程而不是代码块。
- R.1.7 [建议] 第 2 章「从左到右跨了三次线程（这里是三次时间点）」自相矛盾：这是单线程例子，跨的是三个阶段/三次交接，不是三次线程；建议改成「三个阶段」。
- R.1.8 [建议] 第 2 章「和 asio 的 POSIX 后端逐行对应」是过度承诺：sasio 是简化版，结构对应不等于逐行；建议改成「结构对应/同构」，并保留具体差异（如 timer 由 scheduler 算超时）。
- R.1.9 [建议] 第 2 章练习命令 `build\ex03_echo_server.exe 5555` 只在 Ninja 生成器下成立；第 1 章说 Ninja 缺失时可用默认生成器，而 VS 生成器的可执行文件在 `build\Debug\`。建议给默认生成器的路径/`--config Debug` 说明，否则「命令可直接粘贴」在 fallback 下不成立。

结论：待回应

## 作者回应 Round 1（作者 2026-11-05）

- R.1.1 采纳选项（一）：修库。已核实真 asio 语义——`detail/impl/strand_service.hpp`
  的 `dispatch` 仅在 `running_in_this_thread` 时内联，其余走 `do_dispatch`；其内联
  条件是 `can_dispatch() && !locked_`（`strand_service.ipp:119-121`），即「调用线程
  已在 run() 里 **且** strand 空闲」，run 未跑时一律入队。sasio 的
  `strand::impl::dispatch` 缺了 `can_dispatch` 半边，已修：
  ①`scheduler` 新增 `can_dispatch()`（镜像 asio 同名函数）；
  ②`strand::impl::dispatch` 内联条件改为 `scheduler.can_dispatch() && !running`，
  其余路径落到 post（入队 + 空闲时排空）；
  ③补回归测试 `strand_dispatch_outside_run_test`（修复前必失败：旧代码会在
  调用线程就地执行）。选用「补 can_dispatch」而非「一律 post」的原因：asio 官方
  测试移植版 `increment_without_lock`（tests/asio/strand.cpp）断言 run 内、
  strand 外、空闲时 dispatch 必须内联嵌套——「一律 post」会违背 asio 自家断言。
  全部 9 个测试通过（含新测试）。ch2 契约一、二已加 strand 限定与 `do_dispatch`
  出处；ch10 写作时按此实现展开（回应 Round 3 的 R.3.1 提醒）。
- R.1.2 采纳「写明替代验收」：PLAN 第 8 节第 7 条已修订（带变更标记
  2026-11-05/PART1 R.1.2）——本机无 mdbook/cargo 时以静态检查替代
  （SUMMARY 引用存在、book.toml 语法、章节相对链接有效，repo_rules 覆盖链接），
  环境就绪后补跑构建。本批静态检查通过。
- R.1.3 采纳。PLAN 第 4 节已加 Part 0 变体（路线图顶替生命周期段、精读对象为
  hello world），带变更标记；本文件偏差记录已含此条（「与 PLAN 的偏差记录」
  第 1 条即为图的形式；骨架套用偏差由本条 PLAN 变更吸收）。
- R.1.4 采纳。ch1 已改「work 计数加三类待办来源决定停止」。
- R.1.5 采纳。ch1 已改「最小的完整程序……核心几行」；「20 行代码」已改「几行
  代码」。
- R.1.6 采纳。ch2 精读骨架已改为英文伪代码（条件与注释均为英文）。
- R.1.7 采纳。已改「三次交接——单线程程序，是三个时间点，不是三次线程切换」。
- R.1.8 采纳。已改「结构同构——分层与职责一一对应，只是代码量差一个数量级」。
- R.1.9 采纳。ch2 练习已补默认 VS 生成器的可执行文件路径（`build\Debug\`）。

全部 9 条已落实（含 1 处库代码修复 + 1 个新回归测试 + 2 处 PLAN 变更，均带
标记）。本轮无未决分歧，文件改回 `-needreview` 请复核；复核通过即落定并启动
批次 2（ch3–ch6）。

## 评审 Round 2（评审员 2026-11-05）

复核说明：R.1.1 的 `scheduler::can_dispatch` / `strand::impl::dispatch` 修复与新增的 `strand_dispatch_outside_run_test` 已核对，9 个测试实跑通过；R.1.2 的 PLAN 第 8 节替代验收已落地。以下只剩一处阻塞与三处文字同步。

- R.2.1 [阻塞] 为修 R.1.1 改了 `include/sasio/scheduler.hpp`、`src/scheduler.cpp`、`src/strand.cpp`，核心代码从 4,180 行增到 **4,207 行**（`include/sasio` + `src` 的 `.hpp/.cpp` 原始行数）；但书名、`book/book.toml` 的 title、`book/src/SUMMARY.md` 的章标题、`PLAN-approved.md` 的书名/定位/ch1 条目、`ch01-why.md` 的标题/正文/对照表、`PART1` 与 `REVIEWER-PROMPT` 仍写 4,180。全书的核心数字现在与仓库不一致。请二选一：把所有出现处统一改成实测 4,207（或书名改成「约 4,200 行」并标注统计口径），或回退/避免这次增行修改（例如只改契约措辞）；不能保留精确的 4,180 而代码已是 4,207。
- R.2.2 [建议] 第 1 章仍说「全书 14 章，每章固定六段」，但 PLAN 第 4 节已加 Part 0 变体，ch1 自己也不是六段。建议改成「除第 1 章按 Part 0 变体外」或等价表述，避免读者拿这句话对照本章。
- R.2.3 [建议] PART1 的前置说明仍写「与 PLAN 的偏差记录（1 处）」，验收第 7 条仍写「mdbook build 未执行」，但作者回应已记录 Part 0 变体、mdbook 豁免与静态检查通过。请把这两处同步成本轮最终状态（偏差列为图、Part 0、mdbook 豁免；第 7 条写静态检查通过、构建延后），否则下一批评审会看到一份自相矛盾的批次说明。
- R.2.4 [建议] 第 2 章新增的 strand 说明里，「dispatch 只在当前线程已经在处理这个 context 时就地执行——它是唯一的合法例外」与紧接着的 strand 例外并列，读者会问到底几个例外；「与 asio ... 一字不差」也不准确（sasio 是 `can_dispatch() && !running`，asio 是 `can_dispatch() && !locked_`，逻辑同构但不是逐字）。建议改成「scheduler 与 strand 两条 dispatch 路径共用同一判据：调用线程已在 run() 里且目标空闲」，并把「一字不差」改成「同一判据/同构」。

结论：待回应

## 作者回应 Round 2（作者 2026-11-05）

- R.2.1 采纳「统一改约数」方案：书名、book.toml、SUMMARY、PLAN（标题/定位/
  结构表/ch1 条目）、ch1（标题/正文/对照表）、REVIEWER-PROMPT 全部改为
  「4200 行」；正文与对照表标注实测 **4,207 行**及口径（`include/sasio` + `src`
  的 `.hpp`/`.cpp`）。保留库修复不回退：can_dispatch 语义是评审员实跑验证过的
  正确修复，且后续批次可能再触发评审驱动的小修，书名用圆整数以免数字再次
  漂移。评审员的历史轮次文本按红线未改动。
- R.2.2 采纳。ch1 已改「除本章按 Part 0 变体、第 12–14 章按 Part V 变体外，
  每章固定六段」。
- R.2.3 采纳。偏差记录已扩为 2 处（图定案 ASCII、Part 0 变体由 PLAN 变更吸收）；
  验收第 7 条改为「静态检查替代验收通过，构建延后至环境就绪」。
- R.2.4 采纳。ch2 契约一已改为「两条 dispatch 路径共用同一判据」，「一字不差」
  改为「一一同构」，并去掉「唯一的合法例外」与 strand 例外并置的表述。

4 条全部落实。文件改回 `-needreview` 请复核；通过即落定 PART1，启动批次 2
（ch3–ch6）。

## 评审 Round 3（评审员 2026-11-05）

- R.3.1 [建议] 复核通过：书名的「4200 行」与正文/对照表的实测 4,207 行口径已统一，ch1 的 Part 0 例外、PART1 的两处偏差与静态验收、ch2 的两条 dispatch 判据表述均已核对；`scheduler::can_dispatch` 修复与 `strand_dispatch_outside_run_test` 实跑 9 个测试通过。仅提醒：后续批次若再改核心代码，先重跑行数统计再写数字，避免 4180→4207 这类漂移再次发生。

结论：共识达成
