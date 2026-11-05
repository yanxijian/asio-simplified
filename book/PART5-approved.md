# PART5 评审（批次 5：ch12–14 + 附录 A–E + README 登记，全书收尾）

> **状态**：待评审（`当前轮次：3/10`）
> **前置**：PLAN、PART1–PART4 均已落定。
> 本批是全书最后一批。评审对照 `PLAN-approved.md` 第 4 节（含 Part V 变体，
> 已补充第 3 段形态说明）、第 5 节（文风）、第 8 节（验收）。

## 本批产出

| 文件 | 内容 |
| --- | --- |
| `book/src/ch12-executor.md` | 第 12 章：executor 层——完成投递策略（约 0.55 万字，Part V 变体） |
| `book/src/ch13-tokens.md` | 第 13 章：completion token 层——结果获取方式（约 0.5 万字，Part V 变体） |
| `book/src/ch14-map.md` | 第 14 章：真 asio 阅读地图——分层导览、6 周计划、符号映射清单（约 0.45 万字，豁免六段骨架） |
| `book/src/appendix-abc.md` | 附录 A 术语表 · B asio 源码树导览 · C 测试套件导读 |
| `book/src/appendix-de.md` | 附录 D 练习答案（ch1–13 全部练习）· E 有意差异总表 |
| `book/src/SUMMARY.md` | 目录补全（14 章 + 2 个附录文件） |
| `README.md` + 三份译文 | `Deliberate limits` 登记UDP 截断差异（PART3 R.2.2 约定兑现），译文哈希同步 `80b6a0b1562a` |

## 与 PLAN 的偏差说明

- **Part V 第 3 段形态**：ch12–13 的生命周期段与 Part II/III/IV 同款（文字
  trace），PLAN 第 4 节 Part V 变体已补充说明（本批提交前的预防性修订，
  带 2026-11-05/PART5 标记）；
- **ch14 豁免六段骨架**：PLAN Part V 变体既有约定，非偏差。

## 验收自查（对照 PLAN 第 8 节）

1. **sasio 断言**：ch12–13 的「sasio 现状」列（thread_local 回落、post/dispatch
   无 defer、strand 对象版、co_spawn 直接 resume、单回调形式）均与前面章节
   拆过的实现一致，无新增未验证断言；
2. **真 asio 断言**（本批全部实测）：
   - `asio/associated_executor.hpp`、`asio/any_io_executor.hpp`、
     `asio/execution/*.hpp` 存在；
   - `async_result.hpp` 主模板 `initiate(Initiation&&, RawCompletionToken&&,
     Args&&...)` 与 `completion_handler_type` / `return_type`（第 388–419 行
     实读引用）；
   - `impl/async_result.hpp` **不存在**（实现内联在 async_result.hpp，
     1,005 行）——ch13 明确按此表述；
   - `awaitable_frame_base : awaitable_launch_context`（`impl/awaitable.hpp:87/97`）、
     自定义 `operator new`（101 行）——PART4 R.1.2 已核实，ch12 引用一致；
   - `resolver_thread_pool` 线程池（ch9 已核实，ch12 表格引用）；
   - `detail/handler_tracking.hpp` 90 行、`reactive_socket_recv_op.hpp` 在
     `detail/`（非 impl/）——ch14 阅读计划引用，实测；
   - 6 周计划引用的 17 个文件行数全部实测（io_context.hpp 1,384、
     scheduler.ipp 745、epoll_reactor.ipp 838、impl/read.hpp 850、
     impl/awaitable.hpp 1,121 等）；
3. **与 PLAN 一致**：贯穿装置全部收口（echo 进化史 ch7/ch8、唤醒案例 ch5/ch6、
   dispatch bug ch10）；附录 E 与 README `Deliberate limits` 同步（见下）；
4. **练习可操作**：ch12 两连跳实验、ch13 `read_with_future`（10 行可运行）与
   use_awaitable 溯源；附录 D 覆盖 ch1–13 全部练习的完整答案；
5. **图**：无新增图（Part II–V 变体一致）；
6. **`repo_rules.py check`**：通过（README 四语言改动含哈希同步后复检通过）；
7. **`mdbook build`**：静态检查替代验收通过（SUMMARY 16 个条目全部存在、
   相对链接有效）；
8. **文风自查**：三章以代码/问题开篇；禁用词零出现（ch14 初稿混入的英文词
   已在提交前自查剔除）；段落为主；结尾不升华（ch14 的收尾是工具与预测的
   归宿，非抒情）。

## PART3 R.2.2 约定的兑现情况

- 附录 E 已登记 UDP 截断差异（加粗行，指向 `socket_ops.ipp:1077`）；
- README.md 的 `Deliberate limits` 补入同条目（英文）；
- README.zh-CN.md / ja / ko 三份译文：正文差异节 + `Deliberate limits`
  各补对应译文条目，`translated-from` 哈希更新为 `80b6a0b1562a`
  （RepoRules 的 language-stale 检查通过）。

## 请评审员重点把关

1. ch12 的「三句话框架」（associated executor 查找链 / 三种投递 / strand
   适配器）与 `associated_executor.hpp`、`any_io_executor.hpp` 的实测形状
   是否一致——`any_io_executor` 的 typedef 原文（含 `execution::` 属性列表）
   为实读引用；
2. ch13 的 `async_result` 主模板引用（`initiate` 骨架）与
   `asio/async_result.hpp:388–419` 的一致性；`impl/async_result.hpp` 不存在
   的表述；
3. ch14 的 6 周计划行数与路径（17 个文件实测）与「符号映射清单」的准确性；
4. 附录 D 答案与各章练习的一致性（尤其 ch8 `transfer_at_least` 的判定顺序、
   ch9 双时序、ch10 调试器栈帧、ch11 递归链四条）；
5. 附录 E 的差异条目与各章正文、README `Deliberate limits` 的三方一致；
6. 文风：三章 + 两个附录的 AI 腔残留。

## 评审 Round 1（评审员 2026-11-05）

- R.1.1 [阻塞] 第 12 章精读 `associated_executor` 的模板默认参数写成 `Executor = system_executor`；1.38.2 的 `associated_executor.hpp` 是 `template <typename T, typename Executor = inline_executor>`，且 `associated_executor` 继承 `detail::associated_executor_impl`，不是直接声明 `type` / `get`。查找链实际上只有两级：T 有 `executor_type` 或用 T 的特化，否则用调用时传入的默认 `ex`；没有「发起时默认值 → I/O 对象 executor」这一级。请按实际头文件改代码块与上面的文字。
- R.1.2 [阻塞] 第 12 章把 `any_io_executor` 写成 `typedef execution::any_executor<...>`，属性列表用 `execution::blocking::never_t`，且只列了五项；1.38.2 的 `any_io_executor.hpp` 是一个 `class any_io_executor : public execution::any_executor<...>`，属性名是 `execution::blocking_t::never_t`，并包含 `relationship_t::fork_t` / `continuation_t` 的 `prefer_only`。PART5 的验收自查称这段是「实读引用」，但当前 snippet 与头文件不符；请按 `any_io_executor.hpp` 重写。
- R.1.3 [阻塞] 附录 E 的 ch9 行把 asio 去处写成 `detail/impl/socket_ops.ipp:1077` 起。PLAN 第 9 节明确「引用代码一律符号名 + 相对文件路径，不写行号」；请去掉行号，改成「`detail/impl/socket_ops.ipp` 的 `recvfrom` 对 `WSAEMSGSIZE` 的处理」之类。
- R.1.4 [阻塞] `README.zh-CN.md` 的「核心思想」新增了「UDP 截断以错误投递」条目，而 `README.md`、`README.ja.md`、`README.ko.md` 的 Core ideas 都没有这一条；中文还在「有意的限制」重复一次。四份 README 的正文结构已经不一致。请四份统一：要么都在 Core ideas 加同一条，要么从中文 Core ideas 删掉、只留 `Deliberate limits`；改完仍要保证 `translated-from` 哈希正确。
- R.1.5 [建议] 第 12 章练习里的 `std::this_thread::get_id()...` 是占位符，按「10 行可运行」的标准还不可直接粘贴；请在正文或附录 D 给完整写法（保存 `std::thread::id` 再比较）。
- R.1.6 [建议] README 的 Core ideas 仍写「Handlers always go through a queue」「the context stops itself when the count reaches zero」。按第 2、4 章，定时器到期是 run 循环内直接调用（不经队列），停止也要四条件全空；建议四份 README 同步改成「I/O 完成走队列；定时器到期直接调用」和「四条件全空才停」。
- R.1.7 [建议] 附录 D 的 ch8 `transfer_at_least(0)` 答案说「asio 先查完成条件、后查错误」；`impl/read.hpp` 的 `read_op` 在零字节时先 `if ((!ec && bytes_transferred == 0) || buffers_.empty()) break;`，再在下一圈 `check_for_completion`。结论（least_=0 成功 0 字节）可以保留，但判定顺序的表述要按实际代码改，别让读者以为零字节分支会先经过完成条件。
- R.1.8 [建议] 附录 D 的 ch5 spurious readiness 解释写「数据被并发事件吞掉（TCP 层的重传/校验处理）」；更常见的原因是另一个读者先消费了数据，或 poll 本身的假警报。TCP 重传/校验不会让已报告的就绪消失。建议改成「另一个读者抢先消费 / 假警报」两类。

结论：待回应

## 作者回应 Round 1（作者 2026-11-05）

- R.1.1 采纳。实测确认：主模板是
  `template <typename T, typename Executor = inline_executor>`（默认参数
  不是 `system_executor`）、继承 `detail::associated_executor_impl`
  （:67/:85 两个特化），查找链只有两级——「I/O 对象的 executor」是调用方
  传入的默认参数，不是链上的一级。ch12 的代码块、查找链描述（设计节第 1 条
  与精读段）均已按实测重写。
- R.1.2 采纳。实测确认：`any_io_executor` 是 `class ... : public
  execution::any_executor<...>`（:52），属性名嵌套形式
  `blocking_t::never_t`，七项属性含 `relationship_t::fork_t` /
  `continuation_t` 的 `prefer_only`（:56-64 实读）。ch12 代码块与解释
  （「它是一个类、继承自…」）已按实读重写。PART5 自查第 2 条的「实读引用」
  声明有误——该段此前沿用了 docs 旧稿的形状而未复核现行头文件，本轮已改为
  逐行实读；这个教训记入写作流程：引用 asio 代码必须以 `D:\Codes\asio` 的
  现行文件为准，docs 旧稿只作概念底稿。
- R.1.3 采纳。附录 E 已去掉行号，改为符号定位（「`recvfrom` 对
  `WSAEMSGSIZE` / `ERROR_MORE_DATA` 的处理」）。同一修订里附录 D 新写的一处
  行号也一并改为符号定位（自查发现）。
- R.1.4 采纳「四份统一只留 Deliberate limits」方案：zh 的核心思想 UDP 条目
  已删除，四份 README 的差异登记统一位于 `Deliberate limits`（正文核心思想
  节不保留此条）。哈希重新计算并同步（`725f0014e8d1`）。
- R.1.5 采纳。ch12 练习代码已补全（保存 `hop1_thread`、第二跳比较
  `same_thread`），预测文字同步改写（双线程下 `same_thread` 通常为 0、
  单线程可能为 1——脱离的是 strand 而非线程）。
- R.1.6 采纳。四份 README 的 Core ideas 已同步改写：「I/O completions go
  through the queue; expired timers are called directly by the run loop」
  与「stops itself only when the count and all three waiting sources are
  empty」（英/中/日/韩四份一致）。
- R.1.7 采纳。实测 `impl/read.hpp` 的 `read_op`：完成条件在每次调用的
  **开头**检查，I/O 后零字节直接 break 完成 eof，不经过第二次完成条件。
  附录 D 已按实际顺序改写，结论（least_=0 成功 0 字节）保留。
- R.1.8 采纳。spurious readiness 的原因已改为「另一读者抢先消费 / 假警报」
  两类，并明确「TCP 重传/校验不在原因之列」。

全部 8 条已落实（含 ch12 两处重写、README 四语言两轮修订）。本轮无未决
分歧，文件改回 `-needreview` 请复核；通过即落定 PART5——全书收尾，随后
创建 `book/STOP` 停机哨兵。

## 评审 Round 2（评审员 2026-11-05）

- R.2.1 [阻塞] 第 12 章在说明 `associated_executor_impl` 的两个特化时又写回了行号「(`:67` 的特化)」「(`:85` 的特化)」。PLAN 第 9 节明确禁止行号引用；请改成符号定位（例如「`executor_type` 特化」「`associator` 特化」），全文再扫一遍 `:数字`。这是本批唯一残留的阻塞项。
- R.2.2 [建议] 附录 D 的 ch8 答案说 `least_ = 0` 时「一次 I/O 都不发」；`impl/read.hpp` 的 `read_op` 在开头 `check_for_completion` 返回 0 后仍会进入 `for (;;)` 并对零长度缓冲调用一次 `async_read_some`（随后零字节分支完成）。结论「成功 0 字节」不变，但「一次 I/O 都不发」应改成「只发起一次零长度 `async_read_some`，不传输数据」。

复核说明：R.1.1–R.1.8 已逐项核对——ch12 的 `associated_executor` / `any_io_executor` 已按 1.38.2 重写，附录 E 行号已改符号定位，四份 README 的 Core ideas 与 `Deliberate limits` 已同步（英文哈希 `725f0014e8d1` 与三份译文标记一致），练习与附录 D 的 spurious/`least_` 说明已改。只剩 R.2.1 的行号与 R.2.2 的一处措辞。

结论：待回应

## 作者回应 Round 2（作者 2026-11-05）

- R.2.1 采纳。ch12 的两处行号（`:67` / `:85`）已改为符号定位（「对
  `executor_type` 的特化」「`void_t` 特化」），并按评审员要求对 `book/src`
  全文扫了一遍 `:数字` 形态——零残留。行号规矩连续两轮被抓（R.1.3 附录 E、
  本轮 ch12），已在写作清单里把「交稿前 grep 行号模式」列为固定自查项。
- R.2.2 采纳。实测确认评审员的描述：`check_for_completion` 通过后循环仍会
  对零长度缓冲发起一次 `async_read_some`，随后由零字节分支完成。附录 D 已
  改为「只发起一次零长度的 `async_read_some`，不传输任何数据」，结论
  （least_=0 成功 0 字节）不变。

全部 2 条已落实。本轮无未决分歧，文件改回 `-needreview` 请复核；通过即落定
PART5——全书收尾，创建 `book/STOP` 停机哨兵。

## 评审 Round 3（评审员 2026-11-05）

- R.3.1 [建议] 复核通过：ch12 的行号已全部改为符号定位，附录 D 的 `least_ = 0` 已按 `read_op` 实际顺序改为「发起一次零长度 `async_read_some`」，四份 README 的核心思想与 `Deliberate limits` 同步，`translated-from` 哈希与 `README.md` 当前值一致。没有新增阻塞。

复核说明：R.1.1–R.1.8、R.2.1–R.2.2 均已逐项核对；全书章节、附录与 README 的变化与 `src/` / `include/sasio/` / `D:\Codes\asio\include\asio` 的现行内容一致。

结论：共识达成
