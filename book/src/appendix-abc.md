# 附录 A：术语表

按书中出现顺序，全部保留英文原名（正文约定见第 1 章），每条一句话：

| 术语 | 一句话定义 |
| --- | --- |
| synchronous / synchronous call | 调用返回即有结果；函数返回是句号 |
| reactor | 内核只报告「谁就绪」，I/O 由框架自己做（select/poll/epoll/WSAPoll） |
| proactor | I/O 由内核完成，回调拿到的直接是结果（Windows IOCP） |
| handler | 异步操作的完成函数，携带结果或错误 |
| initiating function | 发起函数（如 `async_read_some`），只登记不执行 |
| `io_context` | 服务容器 + `run()` 循环的宿主 |
| service | 挂在 context 上的引擎部件（scheduler、reactor、timer queue） |
| work（单位） | 「还有活干」的记账单位；计数归零且三类待办皆空则 stop |
| work guard | `io_context::work` / `executor_work_guard`，人为持有的 work 单位 |
| op_queue / operation | asio 的万能信封：handler + 分配器 + work + 取消状态的堆上对象 |
| completion signature | 完成签名，如 `void(error_code, size_t)`，决定 handler 形状 |
| executor | 「handler 在哪儿、怎么跑」的一等抽象（ch12） |
| associated executor | handler 自带的 executor 属性，经查找链回落到默认值 |
| completion token | 完成签名下「结果怎么交回」的编译期策略（ch13） |
| `async_result` | token 的特化表：决定发起函数返回类型与完成 handler 形态 |
| strand | 串行化保证：同一 strand 的 handler 不并发且保序 |
| drain（排空循环） | strand 内「取出 → 执行 → 再取」的循环 |
| interrupter | 唤醒管子：写一个字节叫醒睡在 poll 里的线程 |
| spurious readiness | poll 说可读、`recv` 却 `would_block` 的假警报 |
| symmetric transfer | 协程间控制权的栈级平移（不叠栈帧） |
| operation aborted | 取消的完成错误码；取消 ≠ 没有通知 |
| proactor 模拟 | reactor 就绪后由框架补做 I/O，再把结果包装成「完成」 |

# 附录 B：asio 源码树导览

`include/asio`（1.38.2，600 个 `.hpp`、169,063 行）自顶向下的四层，读者按
需要下钻：

```text
asio/
├─ *.hpp                 public API facades (io_context, socket, read, strand…)
├─ impl/*.hpp            those facades' implementations (read.hpp → impl/read.hpp)
├─ ip/、ssl/、ts/        protocols and extensions (ip/tcp, udp, resolver…)
├─ execution/*.hpp       the executor property framework (blocking, outstanding_work…)
└─ detail/               the engine interior (refactor zone; public API ignores its shape)
   ├─ scheduler.ipp      handler queue and run loop (745 lines)
   ├─ *_reactor*         platform I/O backends (epoll/kqueue/select/dev_poll/
   │                     win_iocp/io_uring + null/spin stubs)
   ├─ reactive_socket_*_op  socket operation objects (recv_op 218 lines and up)
   ├─ socket_ops.ipp     Winsock/BSD thin wrappers (4,712 lines)
   ├─ timer_queue*       the timer heap
   ├─ strand_service     strand draining (211 lines)
   └─ handler_tracking   initiation/completion logs (90 lines, ASIO_ENABLE_HANDLER_TRACKING)

```

三条找文件的经验：①`asio/xxx.hpp` 看不懂时去 `asio/impl/xxx.hpp`；②引擎
行为去 `detail/impl/*.ipp`；③同名的 `*_reactor*` 文件按平台读一个即可。

# 附录 C：测试套件导读

sasio 的 9 个测试文件（`tests/asio/`）**移植自 asio 自家测试**（1.38.2 的
`src/tests/unit/`），文件头标注来源与裁剪。这不是形式——第 6 章的堆越界
bug、第 10 章的 dispatch 语义偏差，都是这批测试抓出来的。

| 测试文件 | asio 来源 | 覆盖 |
| --- | --- | --- |
| `io_context.cpp` | `unit/io_context.cpp` | 队列、work、stop/restart、异常传播 |
| `steady_timer.cpp` | `unit/deadline_timer.cpp` | 到期、链式、取消、跨线程唤醒 |
| `ip_tcp.cpp` | `unit/ip/tcp.cpp` + 官方 echo 示例 | no_delay、echo、取消、跨线程唤醒 |
| `strand.cpp` | `unit/strand.cpp` | 嵌套 dispatch、异常恢复、多线程串行、run 外 dispatch |
| `read.cpp` / `write.cpp` | `unit/read.cpp` / `write.cpp` | 部分读写的接力推进 |
| `ip_udp.cpp` | `unit/ip/udp.cpp`（改写为回声用例） | send_to / receive_from |
| `resolver.cpp` | `unit/ip/basic_resolver.cpp`（断言风格参考） | 同步解析、localhost 回环 |
| `coroutine.cpp` | `unit/co_spawn.cpp` / `awaitable.cpp`（模型参考） | co_spawn 返回值/异常、定时器挂起、socket 会话 |

harness（`unit_test.hpp`）有两条值得知道的防线：

1. **断言不弹窗**：Debug CRT 的断言默认弹「Abort/Retry/Ignore」对话框，
   点 Ignore 会让越界程序继续跑完甚至打印 passed。harness 用
   `_CrtSetReportHook2` 把断言改成打 stderr + `_Exit(3)`——立即死，CTest
   必红；
2. **错误计数器**：`ASIO_CHECK` 只累计不中断，suite 结束按计数返回非零
   ——一轮能看到全部失败，而非第一个。

跑全套：`cmake --build build && ctest --test-dir build --output-on-failure`
（9 项，约 10 秒）。第 5、6 章的两个跨线程唤醒回归测试是判别力设计的
范本：修复缺失时必然失败（且不挂起）。
