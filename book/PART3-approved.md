# PART3 评审（批次 3：ch7–ch9，I/O 三部曲）

> **状态**：待评审（`当前轮次：3/10`）
> **前置**：`PLAN-approved.md`、`PART1-approved.md`、`PART2-approved.md` 均已落定。
> 本批覆盖 I/O 三章：ch7 socket 两段式、ch8 组合操作、ch9 UDP 与 resolver。
> 评审对照 `PLAN-approved.md` 第 4 节（骨架 + Part II/III 变体——ch4–6 与
> ch7–9 的第 3 段均为文字 trace）、第 5 节（文风）、第 8 节（验收）。

## 本批产出

| 文件 | 内容 |
| --- | --- |
| `book/src/ch07-socket.md` | 第 7 章：socket——发起函数只描述「就绪后做什么」（约 0.5 万字） |
| `book/src/ch08-composed.md` | 第 8 章：组合操作——operation 套 operation（约 0.5 万字） |
| `book/src/ch09-udp-resolver.md` | 第 9 章：UDP 与 resolver——同一引擎的两种复用（约 0.5 万字） |
| `book/src/SUMMARY.md` | 目录追加三章 |

## 与 PLAN 的偏差说明（1 处，已由 PLAN 变更吸收：2026-11-05/PART3 R.1.4）

- **生命周期段的形态**：ch7–9 与 Part II 同款，第 3 段用代码化 trace 替代
  时序图；PLAN 第 4 节已增补 Part III 变体（PART3 评审 R.1.4，作者采纳）。

## 验收自查（对照 PLAN 第 8 节）

1. **sasio 断言**：ch7 的 `start_read_some` / `do_read` / `complete_read` /
   `start_connect` / `cancel` 代码均摘自 `src/ip_tcp.cpp` 现行实现；ch8 的
   `write.hpp` 精读（接力棒捕获点、move 一次性、零字节防御）对照
   `include/sasio/write.hpp`；ch9 的 resolver `start_resolve` 全文（含
   `aborted` 采样、release 守卫、detach）对照 `src/resolver.cpp`，UDP 槽位
   对照 `src/ip_udp.cpp`；
2. **真 asio 断言**：`basic_io_object.hpp:116` 的 per-context service（本批
   ch7 未再涉及）；`impl/write.hpp` / `impl/read.hpp` 的 `read_op` / `write_op`
   路径（PART2 R.1.4 已核实）；`detail/socket_ops.hpp` + `impl/socket_ops.ipp`
   5,140 行（本批实测，替换了过时的 4,308）；resolver 线程池
   （`detail/resolver_thread_pool.hpp` / `resolver_service.hpp`，本批实测：
   `work_threads_` + `num_work_threads_`，默认 1）；
3. **与 PLAN 一致**：贯穿装置 1 的 echo 进化第 2 形态落在 ch8 实验（手写循环
   → `async_write`）；贯穿装置 3 无新增素材（pop_at 已在 ch6）；第 7 章标题
   与 PLAN 第 7 节 ch7 标题一致；「connect 例外」按 PART2 修订后的不变量②
   表述展开；
4. **练习可操作**：ch8 的 `transfer_at_least(n)` 扩展（约 60 行，含判定顺序
   追问）；ch9 的 UDP echo（30 行内，可运行）与 resolver 双时序推演——
   均可直接粘贴或按步骤操作；
5. **图**：无图（本批三章的生命周期段均为代码化 trace，ch7 直接引用第 2 章
   时序图落点）；无 mermaid 依赖；
6. **`repo_rules.py check`**：通过；
7. **`mdbook build`**：静态检查替代验收通过（SUMMARY 九章引用全部存在、
   链接有效），构建延后至环境就绪（PLAN §8 修订版）；
8. **文风自查**：三章以代码或上一章遗留问题开篇；禁用词零出现；正文段落
   为主；术语保留英文；结尾均为悬念或练习。

## 请评审员重点把关

1. ch7 对「close 注入完成而非撤销」的论述（`cancel` → `complete_xxx` →
   `post`，lambda 按值捕获不引用 socket 成员）是否与 `src/ip_tcp.cpp` 的
   `close()` / `cancel()` / `complete_read()` 顺序一致；
2. ch8 对「状态活到链尾」的机理描述（`shared_from_this` 捕获点、最终 handler
   执行后最后一个引用释放）是否与 `include/sasio/write.hpp` 一致；
3. ch9 对 resolver 五要点（work 先于线程、detach 生存约束、采样即语义、
   post 回环、RAII 归还）是否与 `src/resolver.cpp` 现行实现（含 PART2 评审
   前修复的 aborted 采样）一致；
4. ch8「零字节写是终止条件」与 ch9「数据报截断为简化行为」两处对 asio 行为
   的简化描述是否准确；
5. 文风：三章的 AI 腔残留。

## 评审 Round 1（评审员 2026-11-05）

- R.1.1 [阻塞] 第 9 章说「asio 的行为是丢数据报 + 报错，sasio 简化为截断」，与 1.38.2 相反。`detail/impl/socket_ops.ipp` 的 `recvfrom` 对 `WSAEMSGSIZE` / `ERROR_MORE_DATA` 把 `result` 置 0、清掉错误（POSIX 上返回截断后的字节数），并不产生 `message_size` 错误；`message_size` 只在 `error.hpp` 里定义，没有任何 socket 路径设置它。sasio 的 `socket_ops::recv_from_socket`（`src/socket_ops.cpp`）直接把 `::recvfrom` 的 `SOCKET_ERROR` 映射成错误码，`complete_receive` 会把 `message_size` 交给 handler。请交换/改写这段比较，并重新核对「sasio 简化为截断」是否准确。
- R.1.2 [阻塞] 第 7 章结尾写「第 11 章的组合操作会再次需要『活到链尾的状态』」，但组合操作是第 8 章，第 11 章是协程。请改成第 8 章，否则读者按章节去找会落空。
- R.1.3 [建议] 第 8 章「排了三个 handler（两个中间的、一个最终的）」不准确：两次 `async_write_some` 的完成是两次 post（w1、w2），最终 handler 是在 w2 的调用栈里由 `complete()` 直接调用的，没有再入队。建议改成「两次 post 的中间完成 + 最终 handler 在 w2 里就地执行（仍在 run 循环内）」。
- R.1.4 [建议] PART3 声明「无偏差、三章均按完整六段骨架」，但同一段又说「图：无图」；PLAN 第 4 节六段的第 3 段写的是 mermaid 时序图，而 Part II 变体只覆盖 ch4–6。ch7–9 用文字 trace 可以接受，但需要把「Part III 变体：文字 trace 替代时序图」补进 PLAN 第 4 节（或至少一章补图），否则「无偏差」不成立。
- R.1.5 [建议] 第 9 章 UDP echo 只用一块 `char data[1500]` 同时接收和发送；`async_send_to` 未完成时 `do_receive()` 已经用同一块缓冲登记下一次接收，第二个数据报会覆盖正在发送的内容。建议拆成 recv/send 两块缓冲，或明确把这一点作为练习要发现的 bug，并在答案里说明。
- R.1.6 [建议] 第 9 章绑定端口 7；低端口在部分系统需要特权且可能与系统服务冲突，示例用 5555（与 `ex03` 一致）更稳。
- R.1.7 [建议] 第 7 章说 `complete_read`「只有五行」，实际代码约十四行（清标志、注销、move、post、空 handler 判断）；建议改成「十几行」或删掉行数。

结论：待回应

## 作者回应 Round 1（作者 2026-11-05）

- R.1.1 采纳并致谢——本机实测确认方向确实说反了：`socket_ops.ipp:1077` 等
  六处把 `WSAEMSGSIZE` / `ERROR_MORE_DATA` 显式吞掉（`result = 0`，按成功
  返回实际字节数、错误清零），asio 对齐 POSIX 的静默截断语义；
  `message_size` 只在 `error.hpp` 定义、无 socket 路径设置。sasio 恰恰相反
  （`recv_from_socket` 把 WSAEMSGSIZE 当错误码交给 handler）。ch9 已按真实
  行为改写，并把差异登记为「已登记的行为差异」。
- R.1.2 采纳。ch7 结尾「第 11 章的组合操作」已改「第 8 章」。
- R.1.3 采纳。ch8 已改「post 了两个中间完成；最终 handler 在 w2 的调用栈里
  由 complete() 直接调用——没有第三次入队，但仍在 run 循环内，契约二不被
  破坏」，契约一的兑现表述同步保留。
- R.1.4 采纳。PLAN 第 4 节已加 Part III 变体（带变更标记）；PART3 偏差说明
  改为「1 处，已由 PLAN 变更吸收」，与 PART1/PART2 格式一致。
- R.1.5 采纳，按评审员的第二种方案处理：示例改为收发各一块缓冲 + 每次发送
  拷贝副本（lambda 持 `shared_ptr<vector<char>>` 保活），并在正文直接指出
  共享缓冲的竞态——顺带与第 8 章的接力棒手法呼应。
- R.1.6 采纳。UDP echo 端口 7 改 5555（与 ex03 一致）。
- R.1.7 采纳。「只有五行」改「十几行」。

全部 7 条已落实。本轮无未决分歧，文件改回 `-needreview` 请复核；通过即落定
PART3，启动批次 4（ch10–ch11：strand 与协程）。

## 评审 Round 2（评审员 2026-11-05）

- R.2.1 [阻塞] 第 9 章示例已把绑定端口改为 5555，但验证命令仍是 `nc -u 127.0.0.1 7`；照这条命令连不上，练习无法按文操作。请把命令改成 `nc -u 127.0.0.1 5555`（或与示例一致的变量）。
- R.2.2 [建议] 第 9 章把 UDP 截断差异称为「已登记的行为差异」，但当前 `README.md` 的 `Deliberate limits` 列表没有这一条。请在 README 的差异列表/附录 E 里登记，或去掉「已登记」以免读者找不到。
- R.2.3 [建议] PART3 开头的说明仍写「Part II 变体仅适用于 ch4–6，本批三章恢复完整六段」，与下面的偏差说明（ch7–9 采用 Part III 变体）矛盾；把开头同步为「Part II/III 变体均用文字 trace」，否则读者会以为本批没有偏差。

复核说明：R.1.1–R.1.7 已逐项核对——ch9 的截断比较已按 1.38.2 改写，ch7 的章节引用与 `complete_read` 行数已改，ch8 的 handler 计数已改，PLAN 的 Part III 变体已带变更标记，UDP 示例已改用独立发送副本。只剩上面的命令端口与两处文字同步。

结论：待回应

## 作者回应 Round 2（作者 2026-11-05）

- R.2.1 采纳。ch9 验证命令已改为 `nc -u 127.0.0.1 5555`——示例端口先改、
  命令漏改，正是「两处引用同一常量」该用变量或同一处定义的教训，已在本批
  修订时同步核对全文的端口号。
- R.2.2 采纳「登记进差异总表」方案：ch9 措辞改为「与 asio 的行为差异，将
  统一收录进附录 E 的差异总表（README 的 Deliberate limits 列表在批次 5
  同步补入）」——附录 E（批次 5）会把 `WSAEMSGSIZE` 吞错差异作为正式条目
  收录，届时一并更新四份 README。
- R.2.3 采纳。PART3 头部说明已同步为「Part II/III 变体均用文字 trace」。

全部 3 条已落实。本轮无未决分歧，文件改回 `-needreview` 请复核；通过即落定
PART3，启动批次 4（ch10–ch11）。

## 评审 Round 3（评审员 2026-11-05）

- R.3.1 [建议] 复核通过：端口命令、PART3 头部与 UDP 截断措辞均已同步；R.2.2 把「WSAEMSGSIZE 吞错」差异的正式登记延到批次 5 的附录 E，请在批次 5 真正落到附录 E 与四份 README，避免再次出现「已登记」但读者找不到的情况。

复核说明：R.1.1–R.1.7、R.2.1–R.2.3 已逐项核对；ch7–ch9 的代码引用与 `src/ip_tcp.cpp` / `include/sasio/write.hpp` / `src/resolver.cpp` 现行实现一致。

结论：共识达成
