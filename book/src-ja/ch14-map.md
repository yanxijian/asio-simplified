<!-- translated-from: book/src/ch14-map.md sha256:eb220720e315 -->

# 本物の asio 読書マップ

第 13 章までに、切り取られた 2 層——executor と completion token——はどちらも
分解し終えました。sasio の 9 章（ch3–11）はエンジンと合成層をそれぞれ 1 回ずつ
歩きました。残るは本書の冒頭の約束の最後の 1 つ：この理解を持って、169,063 行の
本物の asio をどう読むか。

本章は 6 段構成の骨格を免除し、3 段構成を取ります：ソースツリーの**階層規約**
（レイアウトを知れば歩き回る時間が半分になります）、**6 週間の読書計画**
（週ごとに明確な目標、検証可能）、そして**シンボル対応リスト**——sasio の
すべての概念が asio のどこに着地するか。記述バージョンは 1.38.2 に固定です。

## 階層規約：ディレクトリが説明書

`include/asio` には 4 つの規約があります。従えばファイル探しの無駄歩きが半分に
なります：

1. **`asio/xxx.hpp` がファサード、`asio/impl/xxx.hpp` が実装**。
   `io_context.hpp` の 1,384 行は恐ろしげですが、大半はドキュメントコメントと
   `#include` です。本物のロジックは `detail/impl/scheduler.ipp`（745 行）に
   あります。`impl/` を見たら「実装はここに住んでいる」とわかります；
2. **`detail/` はエンジン、`asio/` は API**。`detail/` の下のコードは自由に
   リファクタリング・改名・プラットフォーム分岐でき、公開 API には影響しません。
   `asio/` の下のどのヘッダーも約束です。第 3〜11 章で対照した scheduler、
   reactor、timer_queue、reactive_socket_*_op はすべて `detail/` にいます；
3. **`*_fwd.hpp` は宣言だけ**。前方宣言がヘッダー依存を断つために多用されて
   います。「不完全型」で読みが止まったら、`*_fwd.hpp` で本当の宣言がどの
   ヘッダーにあるか探してください；
4. **ファイルはプラットフォームで分かれる**。同名の `epoll_reactor` /
   `kqueue_reactor` / `select_reactor` / `dev_poll_reactor` /
   `win_iocp_io_context` / `io_uring_service` は 1 つの概念のプラットフォーム別
   バックエンドです。1 つを通読すれば 6 つすべてがわかります——同じインターフェイス
   （reactor 意味論または IOCP 意味論）を実装しているからです。

さらに 2 つの「ファイルではない組織」：**operation オブジェクト**
（`detail/scheduler_operation`）は asio の万能封筒です——handler、アロケーター、
work 単位、キャンセル状態を 1 つのヒープ上のオブジェクトにまとめ、op_queue で
並び、scheduler が実行して破棄します。sasio はメンバースロット +
`std::function` で代用しました。その等価性と代償は第 7、8 章で扱いました。
**handler tracking**（`detail/handler_tracking.hpp`、90 行）はデバッグの道具：
`ASIO_ENABLE_HANDLER_TRACKING` を定義すると、発起と完了のすべてがログに
なります（`@asio|時刻|動作`）——「この handler は本当に走ったのか」の問いには
デバッガより速いです。

## 6 週間の読書計画

週ごとに 2〜4 ファイル、4〜8 時間。各週に完了の基準があります。満たせなければ
その週に留まり、急がないでください。

**第 1 週：io_context と scheduler**（本書 ch3–4 と併読）

- 読む：`asio/io_context.hpp`（1,384 行、ドキュメントコメントを飛ばす）、
  `detail/impl/scheduler.ipp`（745 行、重点は `do_run_one`）；
- 基準：「2 つのスレッドが同時に `run()` を呼んだ時、誰がどの handler を実行し、
  `stop()` の後キュー内の handler はどこへ行くか」に答えられる；
- 自己テスト：`outstanding_work_` は sasio の 4 条件とどう対応するか？
  sasio の `maybe_stop_if_idle` は asio のどの行に対応するか？

**第 2 週：reactor（1 つのバックエンドを通読）**（本書 ch5 と併読）

- 読む：`detail/epoll_reactor.hpp`（314 行）+
  `detail/impl/epoll_reactor.ipp`（838 行、重点は `start_op` / `run` / `wait`）。
  Linux 以外なら `select_reactor`（sasio のスナップショットモデルに最も近い
  構造）；
- 基準：「新しい登録はなぜ（またはなぜ必要ない）待機スレッドを起こすのか」と
  「投機的読み（speculative read）とは何か」を説明できる（asio は準備完了時に
  直接 I/O を試み、失敗したら再登録します——sasio の「移し出して再登録」より
  積極的）；
- 自己テスト：sasio の `if (first) interrupt()` は epoll_reactor の中で何に
  対応するか？（答え：epoll のカーネル側登録により新しいdescriptorは自然に
  見える——対応物は `epoll_ctl` + op_queue への入隊であって、起床ではない。）

**第 3 週：socket 操作**（本書 ch7 と併読）

- 読む：`detail/reactive_socket_recv_op.hpp`（218 行）——完全な operation
  オブジェクトの標本；`detail/socket_ops.hpp`（428 行）+
  `detail/impl/socket_ops.ipp`（4,712 行、必要に応じて引く。通読しない）；
- 基準：`async_read_some` の発起から完了までのオブジェクト図を描ける
  （socket → service → reactor op → scheduler キュー）；
- 自己テスト：operation オブジェクトの中に handler のほかに何があるか？
  （アロケーター、work、キャンセルスロット——sasio のスロットはどれが欠けていて、
  欠けると何が起きるか？）

**第 4 週：合成操作と完了条件**（本書 ch8 と併読）

- 読む：`asio/read.hpp`（1,553 行、発起関数の書き方）+
  `asio/impl/read.hpp`（850 行、`detail::read_op`）；`write.hpp` /
  `impl/write.hpp` は対称（1,532 / 743 行）；
- 基準：`transfer_exactly(n)` と `transfer_at_least(n)` の判定の違い、および
  完了条件が op のどのステップで尋ねられるかを説明できる；
- 自己テスト：sasio の練習で書いた `transfer_at_least(n)` を、asio の
  `base_from_completion_cond` はどうポリシーにしたか？

**第 5 週：strand と executor**（本書 ch10、ch12 と併読）

- 読む：`asio/strand.hpp`（579 行）+ `detail/impl/strand_service.ipp`
  （211 行——短いので通読に値します）；`asio/associated_executor.hpp`；
- 基準：「strand の handler はどのスレッドで実行されるか」「`dispatch` の
  インライン条件」「sasio の `can_dispatch() && !running` とどう対応するか」に
  答えられる；
- 自己テスト：sasio の `thread_local current_strand_impl` の asio における
  対応物は何か？（executor の呼び出しスタック比較——`running_in_this_thread`
  は associated executor を通ります。）

**第 6 週：coroutine**（本書 ch11 と併読）

- 読む：`asio/awaitable.hpp`（163 行）+ `asio/impl/awaitable.hpp`
  （1,121 行、重点は `awaitable_frame` の `resume` / `destroy`）；
  `asio/co_spawn.hpp`（525 行）+ `impl/co_spawn.hpp`（449 行）；
- 基準：「coroutineフレームのメモリは誰が管理するか（カスタム
  `operator new`）、co_spawn の work は誰が管理するか（`co_spawn_state` の
  guard）、`use_awaitable` の token はどう完了 handler になるか
  （`async_result` 特殊化——ch13 参照）」に答えられる；
- 自己テスト：sasio の `~promise_type` が completion を呼ぶ——asio の
  対応する動作はどこか？

6 週間以降の拡張読書は必要に応じて：`detail/handler_tracking.hpp`（90 行、
デバッグ）、`io_uring_service`（最新のバックエンド）、`asio/execution/*.hpp`
（プロパティフレームワーク全文——ch12 の深水域）、`src/examples/`
（公式の例、実行可能、トピックごとにフォルダ）。

## シンボル対応リスト

sasio を読み終えた後、これらの概念は asio の中で「一目で認識できる」はずです：

| sasio の概念 | asio の着地点（1.38.2） |
| --- | --- |
| `execution_context` / `service` | `asio/execution_context.hpp` + `detail/service_registry.hpp` |
| `io_context::work` | `executor_work_guard` / `make_work_guard` |
| `scheduler::run_one_impl` | `detail/impl/scheduler.ipp` の `do_run_one()` |
| `handler_work` | `work_cleanup`（operation 完了時の work 解放） |
| `reactor::start_read` / `if (first)` | `select_reactor::start_unready_op` の `if (first) interrupter_.interrupt()` |
| `timer_queue::add` が earliest を返す | `select_reactor::schedule_timer` の `if (earliest)` |
| `timer_queue::pop_at` | `detail/timer_queue.hpp` のヒープ削除（このバグはなし。asio は per_timer_data リスト） |
| `start_read_some` / `do_read` | `basic_stream_socket::async_read_some` → `detail/reactive_socket_recv_op.hpp` |
| `read_op` / `write_op` のリレー | `impl/read.hpp` / `impl/write.hpp` の `detail::read_op` / `write_op` |
| `can_dispatch() && !running` | `strand_service::do_dispatch` の `can_dispatch() && !locked_` |
| `drain()` ループ | `strand_impl::run()` |
| `awaitable<T>` の `promise_type` | `impl/awaitable.hpp` の `awaitable_frame` |
| `co_spawn` の `top_level` + completion | `impl/co_spawn.hpp` の `co_spawn_state` |
| resolver の短命スレッド | `detail/resolver_thread_pool.hpp`（常駐プール） |

このリストは本書の検収基準でもあります：**すべての行について、本書を開かずに
asio のソースで直接見つけられる**こと——それができた時点で、本書は役割を
果たし終えています。

## 締めの言葉

sasio は asio の縮小版ではなく、その**説明の担い手**です：エンジンの章
（ch4–6）は asio がなぜ止まらず、なぜ空回りしないのかを示し、I/O の章（ch7–9）は
発起と完了がなぜ 2 相に分かれるのかを示し、strand とcoroutineの章（ch10–11）は
コールバックの 2 大痛点がどう解決されるかを示し、Part V（ch12–14）は切り取られた
汎用化の機械を一つずつ返却します。

読み終えた後、戻る価値があるのは 2 つだけです：asio のソースそのもの
（唯一の真実の源）、そして練習で書いたあなたの予測——特に間違っていたもの。
非同期プログラミングの難しさは API にあるのではなく、「同時に起きる 2 つの
ことの間に何が起きるか」にあります。本書が教えたすべては、その「間」を
より明確に見せるためのものです。
