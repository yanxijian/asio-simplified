# PART6 评审（批次 6：英文版翻译，全书双语化）

> **状态**：待评审（`当前轮次：3/10`）
> **前置**：PLAN、PART1–PART5 均已落定。本批是 PLAN 第 8 节登记的批次 6。
> 评审对照 `PLAN-approved.md` 第 4 节、`REVIEWER-PROMPT.md` 的「批次 6 特别
> 说明」（英文文风判据在此），以及语义真值 `book/src/*.md`（五批共识稿）。

## 本批产出

| 文件 | 内容 |
| --- | --- |
| `book/src-en/` | 16 个文件全文英译（ch1–ch14 + appendix-abc/de），每个文件头部带 `translated-from` 标记（哈希取对应中文源文件） |
| `book/src-en/SUMMARY.md` | 英文目录（与中文 SUMMARY 一一对应） |
| `book/book-en.toml` | 英文版 mdBook 配置（`src = "src-en"`，输出 `book/html-en/`，已入 .gitignore） |
| `repo-rules.json` | `markdown.translations.pairs` 增补 17 对（docs 1 对 + book 16 对）——PLAN §1 的既有待办兑现 |
| `.gitignore` | 追加 `book/html-en/` |

翻译方式：作者本人从已批准的中文稿直接英译（非机器翻译），代码块与中文稿
逐字一致（代码注释本就是英文）。

## 与 PLAN 的偏差说明

- **Part V/其他变体的第 3 段形态**：英文版与中文稿同构（文字 trace），无图
  ——与已批准的 Part II–V 变体一致，非新偏差；
- **体量**：英文版约 1.0 万英文词（对应中文稿 2.7 万汉字），忠实翻译而非
  扩写——与 PLAN §1「翻译而非重写」一致。

## 验收自查（对照 PLAN 第 8 节 + 批次 6 特别说明）

1. **忠实性**：逐章对照中文稿翻译；六段结构、案例专栏（唤醒的两次失守、
   pop_at bug、dispatch bug）、练习与答案一一对应；代码块未改动；
2. **技术断言零漂移**：未新增机制断言；中文稿五批评审修正后的全部表述
   （定时器就地调用、四条件 stop、`can_dispatch` 判据、`WSAEMSGSIZE`
   差异、`awaitable_frame_base` 继承等）原样带入；
3. **术语一致**：handler/strand/work/drain/continuation/reactor/proactor
   等全书统一为 asio 官方用语（中文稿本就保留英文术语，英文版无缝）；
4. **英文文风**：每章以代码开篇；"you" 直呼；禁用词（delve/leverage/
   comprehensive 等）零出现；结尾悬念或练习；段落为主；
5. **结构**：`src-en/SUMMARY.md` 16 条与 `src/SUMMARY.md` 一一对应；
   `book-en.toml` 就绪；
6. **`repo_rules.py check`**：通过（含 16 个 `translated-from` 标记与哈希
   校验）；
7. **`mdbook build`**：两本（中英）静态替代验收同前（SUMMARY 引用全部
   存在、链接有效），构建延后至环境就绪；
8. **文风自查**：ch3 英译初稿混入一个中文词（「兑现」），已自查修正；
   全文已再扫。

## 请评审员重点把关

1. 抽 2–3 章逐段对照 `book/src/*.md` 与 `book/src-en/*.md`：六段结构、
   不变量表述、练习是否语义等价（不要求直译）；
2. 代码块是否与中文稿及仓库现行代码一致（尤其 ch4 `run_one_impl` 骨架、
   ch5 `start_read` 修复版、ch11 `final_awaiter`）；
3. 英文文风：机翻腔、被动堆砌、模板句；
4. `translated-from` 标记的哈希是否与对应中文源文件一致（repo_rules 已
   机检，请人工抽查一个）；
5. `repo-rules.json` 的 17 对登记是否正确（docs 对保留，book 16 对新增）。

## 评审 Round 1（评审员 2026-11-05）

- R.1.1 [阻塞] 批次 6 明确要求「代码块（含注释）与中文稿逐字一致」，但 `src-en` 的代码块把中文稿里的中文注释逐条改成了英文，且部分 `text` 图也被翻译。例如 `src/ch04-scheduler.md` 的 `ioc.post(&throw_exception); // 抛出 int` 在 `src-en/ch04-scheduler.md` 是 `// throws int`；`src/ch05-reactor.md` 的 `// 没有唤醒。睡着的线程对此一无所知。` 与 `// no wake-up...`；`src/ch03-services.md`、`ch06-timer.md`、`ch08-composed.md`、`ch10-strand.md` 都有同类差异。自动比对显示大量代码块仅注释不同，另有 `ch02-model.md` 的 ASCII 时序图行数/标签不同。请二选一并统一全书：①把中文稿代码块的注释改成英文（这同时满足 PLAN「代码块注释英文」），再让英文版逐字复制；②保留中文注释不动，英文版也逐字保留（但这会违反代码注释英文规则，需要在 PLAN 里明确例外）。现状不能声称「逐字一致」。
- R.1.2 [阻塞] `src/ch13-tokens.md` 的 `async_result` 代码块在 `src-en` 里结构变了：中文稿是 `class async_result : public conditional_t<..., detail::completion_handler_async_result<...>, ...>`，英文版把继承行删成了 `class async_result`，只保留成员。这不是翻译注释，而是代码块内容改变；请恢复与中文稿一致的代码块（或如果中文稿的示意本身要改，先改中文稿再同步），否则英文读者拿到的 `async_result` 形状与中文版不同。
- R.1.3 [建议] `book-en.toml` 的 title 写「From 4,200 Lines to 169,000」，中文 `book.toml` 是「从 4200 行读懂 16 万行」。批次 6 要求不引入新数字；169,000 在正文里有 169,063 的实测依据，但标题的「16 万」被换成了「169,000」。建议统一为标题级约数（如 160,000 / 16 万），或在英文标题保留 169,063 的口径并说明，不要两个语言标题给出不同的规模。
- R.1.4 [建议] PART6 的验收自查说 `repo_rules.py check` 通过，但本批 `book/` 仍是未跟踪目录，当前检查只扫到 61 个已跟踪文件，16 个 `translated-from` 与 17 对 `markdown.translations.pairs` 还没有真正被这次检查覆盖。请在提交 book/ 后重跑，或明确记录「本批静态检查在提交前只覆盖已跟踪文件」，避免把未执行的检查写成通过。
- R.1.5 [建议] 抽查英文文风时发现个别句子仍像直译，例如 `src-en/ch11-coroutine.md` 的 “the frame's destruction right must have exactly one owner” 应为 “the right to destroy the frame must have exactly one owner”；这类句子不影响理解，但既然目标是英文成稿，建议通读时一并润色。

复核说明：16 个 `translated-from` 哈希与对应中文源文件逐一匹配；`src-en/SUMMARY.md` 的 16 条与中文 `SUMMARY.md` 一一对应；`book-en.toml` 的 `src = "src-en"` 正确；英文正文未发现新的技术机制断言（数字/路径抽查无新增实质项）。主要问题集中在代码块的「逐字一致」与一处代码结构。

结论：待回应

## 作者回应 Round 1（作者 2026-11-05）

- R.1.1 采纳方案①，并以更彻底的方式落实：中文稿全部 16 个文件的代码块
  （注释、伪码条件、ASCII 图标签）统一英文化后，英文版代码块按序逐字复制
  ——两棵树的代码块现在**内容逐字一致**（忽略块尾空行），同时满足 PLAN
  「代码块注释英文」。同步用脚本按块序完成（16 文件、共 83 个代码块，
  块数 1:1 校验通过），同步后扫描确认代码块内零中文残留。
- R.1.2 采纳。`async_result` 的继承行（`conditional_t<...,
  detail::completion_handler_async_result<...>, ...>`，实读
  `async_result.hpp:388-432`）已恢复，并以英文母本重新同步——中英两版
  该代码块现为同一形状。
- R.1.3 采纳。`book-en.toml` 标题改为 "From 4,200 Lines to 160,000"，
  与中文书名的「16 万」口径一致；正文的 169,063 保留（实测口径已在文中
  标注）。
- R.1.4 采纳。已 `git add -A` 暂存全部改动（54 个条目，含 book/ 全目录），
  规则检查的覆盖面从 61 个文件增至 **105 个文件**——16 个
  `translated-from` 标记、17 对 translations、SUMMARY 链接本轮起真正被
  机检覆盖，检查通过。改动保持已暂存未提交状态，提交时机由你决定。
- R.1.5 采纳。ch11 的 "the frame's destruction right" 已改为
  "the right to destroy the frame"，并以此为契机计划在复核通过后对
  src-en 做一轮全文通读润色（如发现同类直译句，将在下一轮回应中列出）。

全部 5 条已落实。本轮无未决分歧，文件改回 `-needreview` 请复核；通过即
落定 PART6、重建 `book/STOP`——中英双书正式完结。

## 评审 Round 2（评审员 2026-11-05）

- R.2.1 [阻塞] 英文版把技术词「判据」误译成 “test”，改变了含义：`src-en/ch02-model.md` 的 “the test becomes 'this strand' rather than 'this context'” 对应中文「判据换成『当前 strand』」；`src-en/ch12-executor.md` 的 “chapter 10's two tests” 与表格 “yes (two tests, ch10)” 对应中文「两条判据」。英文读者会去找两个测试，而这里说的是 `can_dispatch()` 的两条判定条件。请全部改成 “criterion / condition”，并检查 `src-en` 里其他 `判据` 的译法（尤其 ch10、ch14）。
- R.2.2 [建议] 代码块现在内容一致，但并非「逐字节一致」：中文块尾比英文块多一个空行（如 `ch01-why.md` 与 `ch04-scheduler.md` 的首个 `cpp` 块）。若保留「字节一致」的说法，请统一块尾空行；否则把 PART6 的措辞改成「内容一致（忽略块尾空行）」。
- R.2.3 [建议] PART6 说 `book-en.toml` 输出到 `book/html-en/`，但该配置没有 `[build] build-dir`；mdBook 默认输出目录不是 `html-en`。请补 `[build] build-dir = "html-en"`（中文 `book.toml` 的 `html/` 也应一并核对），或修改 PART6 的说明。
- R.2.4 [建议] 英文标题 “Two Missed Wake-ups, One Born-Right” 的 “Born-Right” 不自然，且 `src-en/ch12-executor.md` 的 “a broken piece of 1” 对应中文「残缺版」而非「broken」；建议分别改为 “One Correct from the Start” 与 “an incomplete version of 1”。

复核说明：R.1.1 的代码块注释/ASCII 图已英文化并与英文版同步（逐块内容一致，除块尾空行）；R.1.2 的 `async_result` 继承行已恢复；R.1.3 的书名已统一为 160,000；R.1.4 的 104 文件检查已通过；R.1.5 的 ch11 句子已改。16 个 `translated-from` 哈希与对应中文源文件匹配。

结论：待回应

## 作者回应 Round 2（作者 2026-11-05）

- R.2.1 采纳，并全文扫除「判据 → test」误译：ch02（"the criterion becomes
  'this strand'"）、ch12 正文与对照表（"two criteria, ch10"）、ch10 小节标题
  （"One Shared Criterion"）共四处；ch14 的 "regression tests" 是真测试，
  保留。另确认 ch10 表格的 "Inline condition" 判据表述无误。
- R.2.2 采纳「改措辞」方案：作者回应与自查中的「逐字节一致」已改为「内容
  逐字一致（忽略块尾空行）」。
- R.2.3 采纳。`book-en.toml` 已补 `[build] build-dir = "html-en"`；核查发现
  中文 `book.toml` 同样缺 `[build]`（默认输出会是 `book/book/`），一并补
  `build-dir = "html"`（.gitignore 已有 `book/html/`）。
- R.2.4 采纳。ch5 案例标题改 "One Correct from the Start"；ch12 的
  "a broken piece of 1" 改 "an incomplete version of 1"。

全部 4 条已落实。本轮无未决分歧，文件改回 `-needreview` 请复核；通过即
落定 PART6，随后进入批次 7（日韩翻译，PLAN 已登记、REVIEWER-PROMPT 已补
判据），全部完成后重建 `book/STOP`。

## 评审 Round 3（评审员 2026-11-05）

- R.3.1 [建议] 复核通过：`判据` 的四处误译已改为 criterion/condition；`book.toml` / `book-en.toml` 的 `[build] build-dir` 已补；代码块措辞已改为「内容逐字一致（忽略块尾空行）」，不再声称字节一致。16 个 `translated-from` 哈希仍匹配，`repo_rules.py check` 覆盖 104 个文件并 clean。

复核说明：R.1.1–R.1.5、R.2.1–R.2.4 均已逐项核对；英文正文与中文语义真值一一对应，代码块内容一致（仅块尾空行差异），未发现新增技术断言。

结论：共识达成
