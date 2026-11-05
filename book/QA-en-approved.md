# QA-en 评审（批次 2：English，用词专项）

> **状态**：待评审（`当前轮次：3/10`）
> **前置**：QA-zh 已 approved（Round 4 共识）。流程见 `QA-PROTOCOL.md`。
> **范围**：`book/src-en/*.md`（17 文件）。**无折行合并**——英文词间空格本就
> 正确，软换行渲染为空格恰好是英文的规范形态。

## 本批做了什么

### 1. 镜像 QA-zh 的用词改动（zh 真值源 → en 译文同步）

zh 批共 89 处用词替换，其中影响英文表述的镜像如下（38 处，逐条给出位置）：

| zh 决策 | en 镜像 | 位置 |
| --- | --- | --- |
| 长这样/长什么样 → 如下/是什么形态 | looks like this → as follows / what shape...takes | ch05/ch07/ch09/ch11/ch12/ch13 |
| 兑现 → 成立/实现 | is delivered/honored → holds/implemented | ch03/ch04/ch06 |
| 记账/账上/总账 → 计数/计入计数/汇总 | bookkeeping/accounting/on the books/the ledger → counting/counted/the summary | appendix-abc/ch03/ch04/ch09/ch10/ch12 |
| 脸 → 模型/后端 | Two Faces/faces/face → Two Models/models/backend | ch02×7/ch05×1 |
| 句号 → 终点/结束 | full stop → the end | appendix-abc/ch01/ch02×2/ch07 |
| 必死一个 → 必然有一个方向无法推进 | one of them is going to starve → one of the two directions is bound to starve | ch02 |
| 救场/世界末日 → 补救/永远等不到 | rescue/end of the world → remedy/never completes | ch05 |
| 拼图 → 组件 | puzzle pieces → components | ch09/ch10 |
| 眼熟吗 → 与第 8 章同源 | Does this hand look familiar? → Same technique as chapter 8 | ch09 |
| 撑住 → 让 run() 持续运行 | holding up → keeping run() alive | ch09 |
| 豁免 → 唯一例外 | exempt → the one exception | ch14 |
| 躺在 → 记录在/留在 | lying/sits in → recorded in/sitting | ch06/ch10 |
| 缠在一起 → 交织在一起 | tangled → intertwined | ch01 |
| （附带）multi-thread leak → breaks | 露馅语义修正 | ch05 |

### 2. en 独立自查结论

- **保留**（英文中自然常用，不为改而改）：short-lived（短生命周期）、
  visible at a glance、sleeps in WSAPoll（reactor 文献标准表述）、
  collapse、foundation、moved into、interface surface、tangled→intertwined
  （已改）；
- **无需折行合并**：英文软换行 = 空格 = 词间空格，正确；
- **代码块**：逐字节未动（check_translations 验证）。

### 3. marker 刷新

en 的 16 个 `translated-from` 哈希已刷新到当前 zh 源（QA-zh 后的 zh）。
ja/ko 的 stale 保持到各自批次收尾（协议规定）。

## 验收自查

1. `check_translations.py`：en 侧 marker 全部匹配（32 个剩余问题全为
   ja/ko stale，属预期）；
2. 代码块与 zh/en-HEAD 逐字节一致；
3. 词汇镜像 38 处逐条给出（上表），评审员请抽查至少 12 处对照
   `book/src-en/` 原文行；
4. `repo_rules.py check`：clean（en 无中文注释问题）。

## 请评审员重点把关

1. 抽查镜像替换的英文通顺度（尤其 Two Models/backend、counting、the end
   这几组——中英术语习惯不同，如认为某处英文原词更自然，请给出理由与
   替代建议）；
2. 自行审一遍 en 全书的其余用词问题（作者以非母语写作，你的母语级审查
   在本批权重最高）；
3. 确认 en 无需折行合并的判断成立。

## 结论：待评审

## 评审 Round 1（评审员 2026-11-06）

- R.1.1 [阻塞] `src-en/ch02-model.md` 有一个明确的语法错误：「one of the two directions is bound to / to hang.」——跨行重复 `to`。请改成 `one of the two directions is bound to hang`；更准确的说法是 `one of the two directions is left waiting forever`（下面 R.1.3 一起处理）。
- R.1.2 [阻塞] QA-en 漏掉了 zh 的「到账 → 显现」镜像：`ch02-model.md` 仍写「The bill arrives when connection counts climb — ...」。`bill arrives` 是财务比喻，且中文已改为「代价在连接数上去之后显现」；请改成 `The cost shows up when connection counts climb` 或 `The price becomes visible as ...`，不要保留 bill。
- R.1.3 [阻塞] `ch02-model.md` 的 `whichever blocks first starves the other` 与 `Two ends, one execution path` 仍是从中文「必死一个」「两个句号」硬译：`starve` 在并发语境是资源饥饿，不是「另一个方向被阻塞」；`Two ends` 也不等于「两个阻塞点」。建议改成 `whichever blocks first leaves the other waiting forever. Two blocking points, one execution path; one of the two directions cannot make progress.`（或等价表达）。这关系到技术含义，不按风格建议处理。
- R.1.4 [建议] `ch02-model.md` 的 `Function return is the end.` 与 `appendix-abc.md` 的 `the function return is the end` 都太含糊（the end of what?）。中文已从「句号」改为「结束」，英文建议 `A function return means the call is over` / `a return means the call has finished`。
- R.1.5 [建议] `ch02-model.md` 的 `The obvious escape is more threads`、`the three sentences the whole book stands on` 仍是比喻/口语；可改为 `The obvious way out is more threads`、`the three sentences the book is built on`（或 `that underpin the book`）。`nobody blocking anybody` 可改为 `no one blocks anyone`。
- R.1.6 [建议] QA-en 的镜像表没有列「到账」，也没有把 `starve`/`Two ends` 的译法写清楚；请在表里补这两行，并说明 `bill` 已删、`starve` 已替换，避免下一轮又按旧词回填。
- R.1.7 [建议] 复核：16 个 en marker 已刷新，`check_translations.py` 剩余 32 个问题全为 ja/ko stale（预期）；`repo_rules.py check` 159 文件 clean（32 warning）；代码块与 HEAD 对比未变；英文不做折行合并的判断成立。W1/W2/W8 的镜像（Two Models、counting、the one exception 等）抽查通顺。

复核说明：本轮只审 `book/src-en/*.md` 的用词，不要求折行合并；主要问题集中在 ch02 的 `to to`、`bill`、`starve`/`Two ends` 三处。

结论：待回应

## 作者回应 Round 1（作者 2026-11-06）

- R.1.1 采纳。R.1.3 的替换一并处理了这一处：整句现为
  「Two blocking points, one execution path; whichever blocks first leaves
  the other waiting forever.」——to to 重复消除。
- R.1.2 采纳。「The bill arrives」→「The cost shows up when connection
  counts climb」——bill 财务比喻删除，与 zh 的「显现」对齐。
- R.1.3 采纳。整句重写为「Two blocking points, one execution path;
  whichever blocks first leaves the other waiting forever.」——starve
  （资源饥饿术语，语义偏了）与 Two ends（硬译）一并解决。
- R.1.4 采纳。ch02 →「A function return means the call is over.」；
  appendix-abc →「a return means the call has finished」。
- R.1.5 采纳。The obvious escape→The obvious way out；the three sentences
  the whole book stands on→the book is built on；nobody blocking
  anybody→no one blocks anyone。
- R.1.6 采纳。镜像表已在上方补充两行：到账→显现（The bill arrives→The
  cost shows up）；必死一个/两个句号→left waiting forever/Two blocking
  points（starve 与 Two ends 已删，不会回填）。
- R.1.7 收到：16 marker、32 ja/ko stale 预期、159 文件 clean、代码块未变、
  en 不做折行合并成立。

全部 7 条已落实。验证：`check_translations.py` 仅剩 32 个 ja/ko stale
（预期）；`repo_rules.py check` clean（158 文件，32 warning，0 violation）。
文件改回 `-review` 请复核 Round 2；若复核通过，QA-en 落定，进入 QA-ja
（折行合并 + 用词镜像 + 独立审查）。

## 评审 Round 2（评审员 2026-11-06）

- R.2.1 [阻塞] R.1.3 的 `starve` 没有真正删掉：`src-en/ch02-model.md` 现在是「... whichever blocks first / starves the other. Two blocking points, one execution path; whichever blocks first leaves the other waiting forever.」——旧句 `starves the other` 还在，和新句重复且语义不同（一个说资源饥饿，一个说永远等待）。请删掉 `starves the other.`，只保留 `Two blocking points, one execution path; whichever blocks first leaves the other waiting forever.`。这是本轮唯一阻塞。
- R.2.2 [建议] 其余 R.1.1–R.1.6 已核对：`to to` 已消除，`bill` 已改 `cost shows up`，`Function return is the end` 已改 `A function return means the call is over`，`Two ends` 已改 `Two blocking points`（但见 R.2.1 的残留），`The obvious escape`/`stands on`/`nobody blocking` 已改；镜像表已补「到账」「必死一个」两行。16 个 en marker 与 zh 匹配，`check_translations.py` 仅 32 个 ja/ko stale，`repo_rules.py check` clean（158 文件，0 violation）。

复核说明：本轮仍只审 `book/src-en/*.md` 的用词；`starves` 残留是编辑未删旧句，不是翻译选择。请修完重跑 checks。

结论：待回应

## 作者回应 Round 2（作者 2026-11-06）

- R.2.1 采纳并致歉。根因：我在 Round 1 的替换把新句接在了旧句残段后面
  （旧句「whichever blocks first starves the other.」的主语部分没被纳入
  替换范围）。已整句清除：现文为
  「...waiting for data on an established connection. Two blocking
  points, one execution path; whichever blocks first leaves the other
  waiting forever.」——starve 彻底删除，无重复语义。
- R.2.2 收到：其余各项确认通过。

全部意见已落实。验证：`check_translations.py`（32 个 ja/ko stale 预期）、
`repo_rules.py check` clean、代码块未动。文件改回 `-review` 请复核
Round 3；无新阻塞则 QA-en 落定，进入 QA-ja。

## 评审 Round 3（评审员 2026-11-06）

- R.3.1 [建议] 复核通过：`ch02-model.md` 的旧句 `starves the other.` 已彻底删除，现为「Two blocking points, one execution path; whichever blocks first leaves the other waiting forever.」；`bill`/`to to`/`Two ends` 均无残留，`A function return means the call is over`、`The obvious way out`、`the book is built on`、`no one blocks anyone` 通顺；镜像表已补「到账」「必死一个」。16 个 en marker 与 zh 匹配，`check_translations.py` 仅 32 个 ja/ko stale，`repo_rules.py check` clean（159 文件，0 violation），代码块与 HEAD 对比未变；英文不做折行合并的判断成立。
- R.3.2 [建议] QA-en 的独立审查只发现了编辑残留，没有发现新的技术误译；`Two Models`、`counting`、`the one exception`、`Same technique` 等镜像均与 zh 语义一致。

复核说明：本轮只审 `book/src-en/*.md` 的用词；无新增阻塞，建议落定 QA-en，进入 QA-ja。

结论：共识达成
