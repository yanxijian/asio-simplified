<!-- translated-from: book/src/appendix-abc.md sha256:b70095ed69ae -->

# 付録 A：用語集

出現順に、すべて英語の原名を保持します（第 1 章の約定）。それぞれ 1 文で定義：

| 用語 | 一文定義 |
| --- | --- |
| synchronous / synchronous call | 呼び出しが戻れば結果は手の中。関数の戻りが句点 |
| reactor | カーネルは「誰が準備完了か」だけを報告し、I/O はフレームが自分で行う（select/poll/epoll/WSAPoll） |
| proactor | I/O はカーネルが完了させ、コールバックが受け取るのは結果そのもの（Windows IOCP） |
| handler | 非同期操作の完了関数。結果またはエラーを携える |
| initiating function | 発起関数（`async_read_some` など）。登録するだけで実行しない |
| `io_context` | サービス容器 + `run()` ループの宿主 |
| service | context に掛かったエンジンの部品（scheduler、reactor、timer queue） |
| work（単位） | 「まだやることがある」の記帳単位。カウント 0 と 3 つの待ち源がすべて空なら stop |
| work guard | `io_context::work` / `executor_work_guard`。意図的に保持する work 単位 |
| op_queue / operation | asio の万能封筒：handler + アロケーター + work + キャンセル状態のヒープ上のオブジェクト |
| completion signature | 完了シグネチャ。`void(error_code, size_t)` のように handler の形を決める |
| executor | 「handler がどこで、どう走るか」の第一級の抽象（ch12） |
| associated executor | handler が持つ executor の属性。検索チェーンでデフォルトにフォールバック |
| completion token | 「結果をどう受け渡すか」のコンパイル時ポリシー（ch13） |
| `async_result` | token の特殊化表：発起関数の戻り型と handler の形を決める |
| strand | 直列化の保証：同一 strand の handler は並行せず、順序を保つ |
| drain（drain ループ） | strand の「取り出す → 実行する → また取り出す」ループ |
| interrupter | 起こしの管：1 バイト書いて poll で眠っているスレッドを起こす |
| spurious readiness | poll が読めると言ったのに `recv` が `would_block` を返す偽警報 |
| symmetric transfer | coroutine間で制御権をスタックを積まずに移す |
| operation aborted | キャンセルされた完了のエラーコード。キャンセル ≠ 通知がない |
| proactor 模倣 | 準備完了の後、フレームが I/O を補い、結果を「完了」として包装する |

# 付録 B：asio ソースツリーの案内

`include/asio`（1.38.2、600 `.hpp`、169,063 行）を上位から 4 層に。必要に応じて
掘り下げてください：

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

ファイル探しの 3 つのコツ：①`asio/xxx.hpp` でわからなくなったら
`asio/impl/xxx.hpp` へ。②エンジンの挙動は `detail/impl/*.ipp` へ。③同名の
`*_reactor*` はプラットフォームごとに 1 つ読めば、残りも付いてきます。

# 付録 C：テストスイートの案内

sasio の 9 つのテストファイル（`tests/asio/`）は**asio 自家のテストから移植**され
ました（1.38.2 の `src/tests/unit/`）。ファイルヘッダーに出典と削減を記載。
これは儀式ではありません——第 6 章のヒープ越界と第 10 章の dispatch 意味論の
偏差は、どちらもこのテストが捕まえたものです。

| テストファイル | asio の出典 | カバー範囲 |
| --- | --- | --- |
| `io_context.cpp` | `unit/io_context.cpp` | キュー、work、stop/restart、例外の伝播 |
| `steady_timer.cpp` | `unit/deadline_timer.cpp` | 期限切れ、チェーン、キャンセル、クロススレッドの起床 |
| `ip_tcp.cpp` | `unit/ip/tcp.cpp` + 公式 echo 例 | no_delay、echo、キャンセル、クロススレッドの起床 |
| `strand.cpp` | `unit/strand.cpp` | 入れ子 dispatch、例外回復、マルチスレッド直列化、run 外 dispatch |
| `read.cpp` / `write.cpp` | `unit/read.cpp` / `write.cpp` | 部分読み書きのリレー |
| `ip_udp.cpp` | `unit/ip/udp.cpp`（echo 用例に改作） | send_to / receive_from |
| `resolver.cpp` | `unit/ip/basic_resolver.cpp`（アサーションスタイルの参考） | 同期解決、localhost ループバック |
| `coroutine.cpp` | `unit/co_spawn.cpp` / `awaitable.cpp`（モデルの参考） | co_spawn の戻り値/例外、タイマーでのサスペンド、socket セッション |

harness（`unit_test.hpp`）には知る価値のある 2 つの防衛線があります：

1. **アサーションはダイアログを出さない**：Debug CRT のアサーションは通常
   Abort/Retry/Ignore のダイアログを出し、Ignore を押すと範囲外のプログラムが
   走り続け、"passed" を印刷さえします。harness は `_CrtSetReportHook2` を
   インストールして、アサーションを stderr + `_Exit(3)` に変えます——即死、
   CTest は確実に赤です；
2. **エラーカウンター**：`ASIO_CHECK` は中断せずに数え、スイートの終わりに
   カウントで非ゼロを返します——1 回の走行ですべての失敗が見え、最初の 1 つだけ
   ではありません。

スイート全体の実行：`cmake --build build && ctest --test-dir build
--output-on-failure`（9 件、約 10 秒）。第 5、6 章の 2 つのクロススレッド起床
回帰テストは、判別力のあるテスト設計の模範です。修復が欠けていれば確実に
失敗し（しかもハングしない）ます。
