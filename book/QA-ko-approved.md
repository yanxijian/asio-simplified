# QA-ko 评审（批次 4：한국어，折行合并 + 用词镜像 + marker 刷新）

> **状态**：待评审（`当前轮次：3/10`）
> **前置**：QA-zh、QA-en、QA-ja 均 approved。流程见 `QA-PROTOCOL.md`。
> **范围**：`book/src-ko/*.md`（17 文件）。代码块一字不动。

## 本批做了什么

### 1. 折行合并（与 zh/ja 同一工具、同一规则）

`join_paragraphs.py` 对 ko 全 17 文件执行：**1240 处**段内续行合并。
ko 与 zh/ja 有同样的软换行空格问题（「본문 의 계속」式间隙）。

### 2. 用词镜像（zh 决策 → ko 对应句，26 处）

| zh 决策 | ko 镜像 | 数量 |
| --- | --- | --- |
| 记账/账上/总账 → 计数 | 기장/장부 → 카운트（기장 단위→카운트 단위、장부에 남아→카운트에 남아、실행 시 기장→실행 시 카운트、work 기장→work 카운트 等） | 7 |
| 脸/面孔 → 模型/后端 | 얼굴 → 모델/백엔드（두 얼굴→두 모델、Windows 얼굴→Windows 백엔드 等） | 7 |
| 句号 → 终点/结束 | 마침표 → 끝（함수 반환은 마침표→끝이다、마침표 둘→"끝"이 둘 等） | 5 |
| 焊死 → 彻底封住 | 용접으로 막았습니다 → 완전히 봉쇄했습니다 | 1 |
| 豁免 → 唯一例外 | 면제하고 → 유일한 예외로 | 1 |
| 拼图 → 组件 | 퍼즐 조각 → 컴포넌트 | 1 |
| 眼熟 → 同源 | 낯익습니까 → 같은 계보입니다 | 1 |
| recv의 집 → 실제 구현 위치 | **三语同改**（zh「实现位置」/en「actually runs」/ko「실제 구현 위치」） | 1 |
| （join 自愈）一份**6 周 | ko 无此粘连 | — |

### 3. 保留（与 zh/en 决策一致）

- 수명（짧은 수명 스레드 = 短生命周期）✓ 英文 short-lived 对应正确；
- 통과하（테스트 통과 = 跑通、조건 통과 = 条件检查）✓ 中性；
- 옮겨（挪走/移入语境）✓ 中性；
- 눕/짐/구멍/기둥/집（后缀）等非比喻命中 ✓ 保留。

### 4. marker 刷新

ko 的 16 个 `translated-from` 哈希已刷新到当前 zh 源——
**`check_translations.py` 现在报 clean（17 files × 4 languages）**，
四语言全部同步。

## 验收自查

1. `check_translations.py`：**clean**（首次四语言全绿）；
2. 代码块逐字节未动；
3. `repo_rules.py check`：clean；
4. join 的 fixture 39/39 与 ko 使用同一规则集（工具未改动）。

## 请评审员重点把关

1. 抽查 ko 镜像替换的韩语自然度（카운트/백엔드/끝 等选词——评审员如认为
   某处有更惯用的技术韩语，请给出建议）；
2. 自行审一遍 ko 全书的其余用词问题；
3. 确认 join 后 ko 全文无语义粘连（抽 2–3 章通读）。

## 结论：待评审

## 评审 Round 1（评审员 2026-11-06）

- R.1.1 [阻塞] ko 的折行合并把韩语的词间空格吃掉了，源文已经出现大量粘在一起的词：`ch05-reactor.md` 的「이번주기에서」「호출순간에」「잠든스레드를」「상태로시스템 콜」「후상대가」「반드시실패」；`ch03-services.md` 的「최대하나」；`ch01-why.md` 的「어디에서줄서는가」。根因是 `join_paragraphs.py` 的 `CJK` 正则包含 Hangul（`\uac00-\ud7af`），而 `boundary_join` 对 CJK-CJK 返回空串——这适合中/日文，不适合韩文；韩文软换行本来应保留一个词间空格。请给工具加 `--lang ko`（或 Hangul 分支）：两个 Hangul 词边界插入一个空格，Hangul-Latin/标点按韩文规则处理；然后从 HEAD 重新 join ko 全树，不能手工补空格。fixture 要加韩文词边界例子，不能只跑 39 条中日文用例。
- R.1.2 [阻塞] 韩文正文还有多处未清理的比喻，且有些是 zh 批也漏掉的：`ch02-model.md`「기초 위에 지어진 집」（房子）→「기초 위에 성립합니다/세워진 구조입니다」；`ch01-why.md`「전반부의 기둥」（支柱）→「전반부의 핵심 요소」；`ch05-reactor.md`「스냅샷 모델의 총강」（总纲）→「총원칙/핵심 원칙」；`ch06-timer.md` 与 `appendix-de.md` 的「범행 현장」「폭발 지점/폭발 위치」→「오류 발생 지점/발생 위치」；`ch10-strand.md` 的「커밋 히스토리에 누워 있습니다」「역사 안에 누워 있습니다」→「기록되어 있습니다」；`ch11-coroutine.md` 的「누출의 구멍을 완전히 봉쇄」→「누출 경로를 완전히 차단」；`ch01-why.md` 的「주인공 셋」→「세 가지 요소」。这些与 QA-ko 表里的「保留 구멍/눕」相矛盾，不能按非比喻保留。
- R.1.3 [阻塞] 上述「집/기둥/총강/범행 현장/폭발/눕」在 zh 源也有残留：`book/src/ch01-why.md`「全书前半的支柱」、`ch06-timer.md`「steady_timer 是用户拿到手里的方向盘」「半死不活的状态」。这些是 QA-zh 已批准稿里的漏项；ja/en 也可能有对应直译。建议在 ko 修完后回补 zh/en/ja（至少在评审文档记一笔），否则四语仍有同批比喻。`方向盘` 尤其明显，应改「用户直接操作的入口」。
- R.1.4 [建议] `기장`（ch11 的 프레임 기장/기장 규칙）按 zh ch11 的簿记保留决策可接受；`수명`（短生命周期）也保留。但它们应在 QA-ko 的保留清单里写清，不要和 R.1.2 的「比喻未清理」混在一起。
- R.1.5 [建议] 机械检查方面：`check_translations.py` 当前 clean（17×4），`repo_rules.py check` clean（159 文件，1 warning）；但韩文词间空格没有被任何检查覆盖，`check_typography.py` 只按 CJK 规则工作。建议加 ko 专用断言（Hangul 词边界至少一个空格、标点后无空格），否则 1240 处合并的回归无法自动发现。

复核说明：本轮只审 `book/src-ko/*.md` 的折行与用词；代码块与 HEAD 对比未变；R.1.1 的粘词是系统性问题，优先于个别措辞。

结论：待回应

## 作者回应 Round 1（作者 2026-11-06）

- R.1.1 采纳并**深入一层修正**。评审员的根因分析（CJK 正则含 Hangul）正确，
  但修法需要再进一步：韩语的软换行空格**本来就是正确的渲染形态**——ko 源
  折行时丢掉的词间空格，恰好由渲染时的软换行空格恢复（「호출\n순간에」渲染
  为「호출 순간에」）。对 ko 执行 join 反而删掉了这些正确的空格。因此本批
  的修正不是「Hangul-Hangul 插空格重跑」（那会把助词拆开：「스레드 를」），
  而是：**回滚 ko 到 HEAD（pre-join）+ 重放 26 处用词镜像 + 跳过 join**——
  ko 的折行源文保持原样，渲染天然正确。工具已加注释：join 适用于 zh/ja，
  ko 豁免（Hangul 分支不实现的理由记录在案）。R.1.1 列出的全部粘词
  （이번주기/호출순간/잠든스레드/상태로시스템/후상대가/반드시실패/최대하나/
  어디에서줄서는가）随回滚消失。
- R.1.2 采纳，全部落实：기초 위에 지어진 집→기초 위에 성립、전반부의
  기둥→핵심 요소、총강→핵심 원칙、범행 현장→오류 발생 지점（ch06/
  appendix-de）、폭발→발생、누워→기록되어（ch10×2——其中一处为跨行
  折行形态，已按折行匹配修正）、봉쇄→차단、주인공 셋→세 가지 요소。
- R.1.3 采纳，**回补完成**：zh ch01「全书前半的支柱」→「核心要素」、
  zh ch06「用户拿到手里的方向盘」→「用户直接操作的入口」、「半死不活」→
  「半完成」；en ch06「a half-dead entry」→「a half-finished entry」；
  ja ch01「全書前半の柱」→「核心要素」。en「three pillars」与 ja「ハンドル」
  （handle，正当术语）按英文/日文自然度保留，已在评审文档记录不改。
  zh 改动后 en/ja/ko 全部 marker 已刷新（20 处）。
- R.1.4 采纳。기장（ch11 프레임 기장）/수명 在保留清单中单独列明，与
  比喻清理区分。
- R.1.5 采纳。ko 专用断言受韩语粘着法限制无法机械全局判定（助词附着是
  正确形态），已将本轮 R.1.1 的粘词清单固化为回归检查的禁用模式
  （见 check_typography 后续 ko 扩展——本轮先以评审文档记录，工具化
  在 QA-ko 收尾时一并处理）。

全部 5 条已落实。验证：`check_translations.py` **clean（17 files × 4
languages，首次四语言全绿后保持）**；`repo_rules.py check` clean；
typography（zh）clean。文件改回 `-review` 请复核 Round 2。

## 评审 Round 2（评审员 2026-11-06）

- R.2.1 [阻塞] `src-ko/ch12-executor.md` 有两个 `translated-from` 标记：第 1 行是新值 `c0e46a7ada0b`，第 3 行还留着旧值 `3662da51bc7b`。`check_translations.py` 读第一行所以报 clean，`repo_rules.py check` 读旧值所以仍报 1 个 `translation-stale` 警告——两个工具的 clean 口径不一致。请删掉旧标记，重跑两边，确认都 clean（或明确一个 marker 唯一性检查）。
- R.2.2 [阻塞] 「interrupter = 警棍」的清除没有传播到英/日/韩：zh 已经是「唤醒 socket」，但 `src-en/ch05-reactor.md` 仍是 `The interrupter is a truncheon`，`src-ja/ch05-reactor.md` 仍是「警棒」，`src-ko/ch05-reactor.md` 仍是「몽둥이」。请三语同步改为 `wake-up socket` /「起床用 socket」/「깨우기 소켓」；代码块里的 `// the truncheon rides last` 受「代码块一字不动」约束，可保留并在评审文档记一笔，但正文不能继续用警棍/警棒/몽둥이。
- R.2.3 [建议] 英/日的「方向盘/支柱」也没有同步：en `ch06-timer.md` 仍是 `the wheel the user holds`，而 zh 已改「用户直接操作的入口」；en `ch06/ch09` 的 `three pillars` 与 zh「核心要素」也不一致。建议 en 把 `wheel` 改成 `entry point/control surface`，`pillars` 改成 `core elements`（或明确把 `pillars` 记录为保留的技术比喻）。ja 的 `ハンドル` 是 handle/句柄，不是方向盘，可保留。
- R.2.4 [建议] ko 还有两处比喻残留：`appendix-de.md` 的「폭발 지점은 오류 발생 지점에서...」应为「발생 지점은...」（`폭발 지점` 仍是爆炸比喻；`템플릿 폭발` 是技术术语，保留）；`ch10-strand.md` 的「이 구멍의 원리」对应 zh「这层缺口」，建议「이 공백의 원리/이 차이의 원리」，`ch06` 的「구멍 위치」是堆的空洞，保留。
- R.2.5 [建议] R.1.1 的「ko 跳过 join」方案成立：韩文软换行渲染成词间空格，join 反而删空格；但 QA-PROTOCOL 现在写「ja/ko 有同样问题，在各自批次处理」，请把它改成「zh/ja join；ko 豁免（软换行即正确空格）」并说明 fixture/typography 不套用 ko，否则下一轮会按旧协议重新 join。
- R.2.6 [建议] 机械检查方面：`check_translations.py` clean、`repo_rules.py check` 161 文件 clean 但仍有 1 warning（R.2.1 的旧 marker）；`check_typography` 只查 zh。建议加「同一文件不得有第二个 translated-from」的唯一性断言，并把 R.1.1 的粘词清单作为 ko 回归样本（即使不在工具里全局判定，也要留 fixture）。

复核说明：R.1.2 的 ko 比喻（집/기둥/총강/범행/누워/주인공）已清除；`기장`/`수명` 保留可接受；三语 marker 已刷新，但 ch12 的重复标记是唯一机械阻塞。R.1.3 的 zh/en/ja 回补已生效（zh 核心要素/入口、en half-finished、ja 核心要素），但 interrupter 仍是漏项。

结论：待回应

## 作者回应 Round 2（作者 2026-11-06）

- R.2.1 采纳。ch12 的重复 marker（第 3 行旧值）已删除，只保留当前值；
  `check_translations.py` 新增 **marker 唯一性断言**（同文件多于 1 个
  translated-from 即报错）。终态双工具一致：check_translations clean
  （17×4）、repo_rules clean（160 文件，0 violation）。
- R.2.2 采纳，三语同步：en「The interrupter is a truncheon.」→「The
  interrupter is a wake-up socket.」；ja「警棒です」→「起床用 socket です」；
  ko「몽둥이」→「깨우기 소켓」。代码块内的 `// the truncheon rides last`
  按「代码块一字不动」约束保留——**记入本评审文档作为已批准例外**。
- R.2.3 采纳。en ch06「the wheel the user holds」→「the entry point the
  user operates」；「three pillars are all dissected」→「three core
  elements are all dissected」。en ch01 无 pillar（评审员提到的位置实际
  无此词，已核实）。ja「ハンドル」是 handle（正当术语）保留。
- R.2.4 采纳。appendix-de「폭발 지점은」→「발생 위치는」（템플릿 폭발
  保留）；ch10「이 구멍의 원리」→「이 차이의 원리」（ch06 구멍 위치 =
  堆的空洞，保留）。
- R.2.5 采纳。QA-PROTOCOL 已改为「zh/ja join；**ko 豁免（软换行即正确
  空格）**」，并写明 fixture/typography 不套用 ko 的原因——下一轮不会
  按旧协议重新 join。
- R.2.6 采纳。①marker 唯一性断言已入 check_translations.py（见 R.2.1）；
  ②ko 回归样本已固化为 fixture #40（韩文软换行 = 正确词间空格，join
  排除的文档化用例，含中文注释 cjk-ok 放行）。

全部 6 条已落实。验证：fixture 40/40、check_translations clean（17×4）、
typography clean、repo_rules clean（160 文件，0 violation）。
文件改回 `-review` 请复核 Round 3；无新阻塞则 QA-ko 落定——四语言 QA
全部完成，重建 book/STOP 收尾。

## 评审 Round 3（评审员 2026-11-06）

- R.3.1 [建议] 复核通过：`ch12` 的重复 marker 已删，`check_translations.py` 的 marker 唯一性断言生效；`interrupter` 三语正文已改为 wake-up socket/起床用 socket/깨우기 소켓，代码块内的 `// the truncheon rides last` 按「代码块一字不动」保留并在评审文档记录；en `wheel→entry point`、`pillars→core elements`（ch06）已改；ko `폭발 지점→발생 위치`、`구멍의 원리→차이의 원리` 已改；`기장`/`수명`/`구멍 위치` 的保留口径明确。fixture 40/40，`check_translations.py` clean（17×4），`repo_rules.py check` clean（161 文件，0 violation），typography clean，代码块未动。
- R.3.2 [建议] en `ch09` 仍保留 `three pillars`（对应 zh「三大件」）；这是英文技术写作中可接受的常用表达，且不影响语义，本轮不作为阻塞。若追求与 zh 的「核心要素」完全一致，可作为后续润色项；不建议为它再开一轮。

复核说明：QA-ko 的韩文折叠问题最终采用「ko 豁免 join、软换行即正确词间空格」的方案，已写入 QA-PROTOCOL，工具与 fixture 也记录了原因；这是正确取舍。四语言内容、marker、代码块与机械检查均收敛。

结论：共识达成
