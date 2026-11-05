<!-- translated-from: book/src/appendix-de.md sha256:60f97c02825b -->

# 付録 D：練習解答

各章の練習の完全な解答です。まず自分でやってください——答えは逃げません。

## ch1 実験：ビルドとテスト

コマンドは本文のとおり。よくある失敗：Ninja がない場合はデフォルト
ジェネレーターに（実行ファイルは `build\Debug\` へ）；`ws2_32` のリンク
エラーは VS コマンド環境にいないことを意味します。

## ch1 バグハント：例外用例の 3 問

1. 例外を投げた handler は**すでにデキュー済み**で、例外とキューは無関係です。
   正常な handler はそのまま保たれ、catch 後の再実行で続きます；
2. `stopped()` は **false**。例外は停止指示ではありません：停止は「四条件全空」
   か明示的な `stop()` だけで起きます。例外が飛んだ時キューにはまだ仕事があったので、
   停止フラグは一度も立ちませんでした；
3. 各 `run()` は最低 1 つの handler を処理してから中断されます；3 つの正常
   handler は 3 周に分かれて実行され、その後キューが空になり、四条件が揃い、
   `run()` が 0 を返して正常に終わった時、`break` が条件を満たします。

## ch2 実験：echo サーバの並行

2 つの接続は同時にさばかれます：socket ごとに reactor のdescriptorテーブルへ
登録され、1 回の `WSAPoll` が複数の準備完了を報告し、ループは順に処理します。
接続して送らないクライアントは準備完了を生まず、システムコールを浪費しません。

## ch2 バグハント：cancel の通知の時機

handler は `cancel()` の呼び出しスタックの中では実行されません——`post` の後、
run ループが実行します。エラーコードは `operation_aborted` です。もし呼び出し
スタックの中で同期的に呼ばれ、handler が呼び出し者が保持している状態に触れたら、
それが再入です。「キャンセルは必ず post を通る」がこの経路を取り除きます。

## ch3 実験：handler_stats サービス

2 つの context がそれぞれ `handler_stats` を掛けても干渉しません：不変量 1 の
完全な表現は「サービス型ごと、**context** ごとに高々 1 つ」——サービスの鍵は
`(context, type)` であって、グローバルな type ではありません。

## ch3 バグハント：recursive_mutex

普通の `std::mutex` に変えると、デッドロックは**2 回目**の `use_service` 進入で
起きます：`io_context` 構築 → `use_service<scheduler>()` がロックを取り →
scheduler が構築 → `use_service<reactor>()` が同じロックを再び取る。スタック
トレースには 2 段の `use_service` フレームと 1 か所の `std::mutex::lock`
が見えます。再帰的構築チェーンは、需要に応じた組み立ての直接の代償です。

## ch4 実験：work リーク

引っかかったループ：`work_count_` は 1（guard が保持）、handler が走り終えた後
キューは空、timer heapは空、descriptorなし——しかし `has_work_locked()`
は `work_count_ != 0` のため真であり、`run_one_impl` の毎周は第 3 段まで進み、
「まだ仕事がある」と計算して `reactor_.run_for(max)` で眠り、誰にも起こされません。
`reset()` 版：2 つ目の context は四条件が `reset()` 後すべて揃い、`run()` は
**0** を返します。

## ch4 バグハント：例外の 3 問

第 1 章の答えを見てください（同じ用例です）。要点の復習：デキューされた handler は
キューと切り離され、例外は停止フラグを立てず、3 つの正常 handler は 3 周に
分かれて実行されます。

## ch5 実験：1 回目の失守の再現

2 か所の `if (first) interrupt()` をコメントアウトすると、
`tcp_multithread_wakeup_test` は約 5 秒後に失敗します：run スレッドは
`WSAPoll(∞)` で眠り、新しく登録された読みはそのスナップショットにありません。
唯一の目覚め源は 5 秒後のウォッチドッグタイマーです。復元すれば即座に通ります。
教訓：スナップショットモデルの新しい待ち源は、待っている者を起こさなければ
なりません。

## ch5 バグハント：spurious readiness

2 つの原因：**別の読み手が先に消費した**（同じデータグラムを別の `recvfrom` が
取った、またはエンジンの投機的読みが先に I/O をした）、または**偽警報そのもの**
（poll が約束するのは「おそらく読める」だけです）。TCP の再送やチェックサムは、
報告済みの準備完了を消しません——原因のリストには入っていません。再登録しない
場合の帰結：コールバックはすでに表から出ており（不変量 2）、この読み操作は
reactor の世界から消えます——バッファは二度と準備完了の通知を受け取れず、
**静かな読みの停止**になります。

## ch6 実験：pop_at の越界

再現の要点：2 つのタイマー（50ms / 300ms）。先に期限切れになるものが
`pop_at(0)` を実行し、ヒープサイズ 2 で `index(0) != last(1)` なので、交換の
経路で削除された entry の `heap_index` が 1 になります。`pop_back` の後、
`heap_.size() == 1` で、汚れたインデックス `1 == size()` が確定します。アサーション
が飛び出すのは**その後**です：2 つのタイマーオブジェクトの破棄時、`cancel()` →
`remove()` が汚れたインデックスをヒープに適用し、Debug STL の
`vector subscript out of range` が飛び出します。爆発点は犯行現場から遠い——
インデックス型バグの典型的な特徴です。

## ch6 バグハント：remove 失敗の 3 つの理由

- インデックスが `npos`：entry はすでにヒープの外——出る道は 1 つ
  （`take_expired` または `pop_at`）で、どちらも handler の完了は手配済みです
  （成功の完了は scheduler が直接呼び、投げる途中にあります）；
- インデックス越界：entry はすでにヒープの外（上と同じ）。または汚れた
  インデックス（修復後は存在し得ない。防御チェックが後ろ盾）；
- 身元不一致（`heap_[index] != e`）：インデックスが別の entry に属する——ヒープ内
  の交換は `swap_at` がインデックスを同期するので、起こるはずがありません。
  防御として残されています。
  3 つの場合すべてで、handler の完了にはすでに受け手がおり、失われません。

## ch7 実験：2 つの session の交差

交差します。各 session の `async_read_some` の完了は独立したキュー項目です。
2 つの socket が同時に準備完了になると、`WSAPoll` が両方を報告し、`do_read` が
順に同じスレッドで実行され、2 つの完了 handler が順にキューに入って実行されます。
シングルスレッドの `run()` の並行モデルは「交差」であって「並列」ではありません。

## ch7 バグハント：close 後の新しい発起

`close()` はすでに `handle_` を `INVALID_SOCKET` にしているので、新しい
`async_read_some` は `start_read_some` の `!is_open()` 分岐を通ります：handler は
`bad_descriptor` を携えて `post` されます。順序：先に投げられた
`operation_aborted` がキューの前にあり、後から投げられた `bad_descriptor` が後です
——**キューの FIFO が順序を保証します**。両方の通知が届き、それぞれのバッファの
有効性は呼び出し側の責任です。

## ch8 実験：状態は write_op に移りました

手書き版の `write_offset_`/`write_size_` は session のメンバーでした——
session ごとに 1 つ、生存期間は session と同じ。合成版の対応する状態は
`write_op` の `transferred_` です——`async_write` ごとに 1 つ、生存期間は
shared_ptr のリレーが管理し、チェーンの終わりで自動的に破棄されます。session が
持つ必要がないのは、それがこの 1 回の書き込み操作だけに属するからです。

## ch8 拡張：transfer_at_least(n)

`read_op` / `write_op` に `least_` メンバーを追加し、完了判定を
`transferred_ == buffer_.size()` から
`transferred_ >= least_ || transferred_ == buffer_.size()` に変えます。
`least_ = 0` とデフォルトの関係は、asio の**実際の順序**に従います
（`impl/read.hpp` の `read_op`）：呼び出されるたびに**まず**完了条件を検査し
（`operator()` 冒頭の `check_for_completion`）、I/O の後に 0 バイトを読んだら
**直接 break して eof で完了**します（I/O 後のゼロバイト分岐で、2 回目の完了条件を
通りません）。したがって：`least_ = 0` なら発起の前の検査の時点で条件が成立し、
成功の 0 バイトを返します——ただし**I/O が 1 回もないわけではありません**：条件が
通った後もループは 1 回のゼロ長の `async_read_some` を発起し、ゼロバイト分岐が
完了させます。データは動きません。EOF のケースで `least_ > 0` なら eof エラーです。
完了条件の検査を I/O の前に置くことで、この順序に揃えられます。

## ch9 実験：UDP echo と並行

`async_send_to` が未完了の間に、次の `async_receive_from` はすでに登録されて
います——2 つの方向が独立したスロットを占め、**並行して進められます**。TCP 版と
の違い（session の同一方向は直列——書き切ってから読む）は、UDP にストリームが
ないことから来ます：受信と送信は互いに依存せず、エンジンは両方向を独立に登録
します。例の `payload` コピーはまさにこのためです。共有バッファは次の
データグラムに上書きされます。

## ch9 バグハント：2 つのタイミング

- タイミング A：サンプリングは `getaddrinfo` が戻った後、post の前に
  行われます——`cancel()` がすでにフラグを裏返しているので、サンプルは true →
  handler は `operation_aborted` を受け取ります（解析結果は捨てられます）；
- タイミング B：サンプリングの後で `cancel()`——フラグの反転は、すでに
  キャプチャされた `aborted` の値に触れません → handler は解析結果を受け取ります。
  サンプリングが**スレッドの中**で行われる理由：post は非同期です。handler の中で
  サンプリングしたら、「キャンセルが届いた」と「サンプルされた」の前後は意味論では
  なくスケジューリングの遅延で決まり、キャンセル通知は丸 1 つのキュー周期だけ
  遅れ、タイミング B もキャンセルと誤報され得ます。

## ch10 実験：strand をバイパスする

`s.post` を `ioc.post` に変えると、handler は scheduler のキューに直接入り、
2 つの `run()` スレッドがそれぞれ取り出して実行し、`max_active` のピークは 1 を
超えます（正確な値はスケジューリング次第で、通常 2）。strand 版の一定の 1：
`running` フラグが並行を直列に圧縮します。「どんな状態に strand が必要か」の
判別：複数の handler が読み書きし、リクエストが並行して届き得る状態です。

## ch10 バグハント：dispatch 偏差の再現

`!running` 版では、`strand_dispatch_outside_run_test` の**最初のアサーション**
が先に失敗します（`ASIO_CHECK(!ran)`）——handler が `s.dispatch` の呼び出し
スレッド（メインスレッド）でインライン実行されたからです。デバッガでスタックを
底から読むと：テスト関数 → `strand::impl::dispatch` → `execute` → handler で、
スタックの中に `run` / `run_one_impl` は**ありません**——handler は run ループの
中になかった。`scheduler.can_dispatch() && !running` に戻すと、両方のアサーション
が通ります。

## ch11 実験：再帰チェーンとスタックの深さ

再帰の `chain(1000)` はスタックを一度も増やしません：各層がサスペンドすると
制御権はエンジンに返り、子が完成すると symmetric transfer が親に移ります
——1,000 回の上りは 1,000 回の移動であって、1,000 個の積まれた呼び出しでは
ありません（デバッガで最も深い所にブレークポイントを置くと、スタックは数フレーム
しかありません）。`final_suspend` を `suspend_always` に変えると：最も深い
フレームがサスペンドしたまま誰も再開せず（トップレベルではない、completion も
ない）、チェーン全体が凍結します。最も深いタイマーはまだヒープを占めますが、
他の層は永遠に再開を待てません——`run()` はタイマーが完了した時点で四条件が空に
なり停止し、テストはハングまたはタイムアウトします。これが「誰にも回収されない
フレームはリーク」です。

## ch11 バグハント：move-only

コンパイルエラーのキーワードは**右値参照 / `operator co_await() &&`** です——
awaitable は右値しか受け付けません。正しい書き方：`co_await std::move(task);`。
move-only がコピーを許すより安全な理由：コピーされた 2 つの awaitable が同じ
coroutineハンドルを共有し、どちらも co_spawn されたら——1 つ目の spawn が
フレームを取り、completion を設定し、2 つ目の spawn が completion を上書きし、
実行中または破棄済みのフレームを再び resume します。所有権が一意でなければ、
破棄権も一意になりません。

## ch12 実験：2 つの hop

hop 1：`running_in_this_thread()` は 1（手動ラップが効いています）。hop 2：0
——ラップが包んだのは hop 1 の完了 lambda だけです。hop 2 の
`timer.async_wait` の完了には strand の情報がなく、どの run スレッドにも
届きます。asio の `bind_executor` は strand を handler の associated executor に
書き込むので、検索チェーンが層ごとに効きます——これが sasio の手動ラップと
asio の体系との差です。

## ch12 バグハント：strand のインターフェイス面

asio `strand.hpp` には 4 つの配送メンバーのほかに、`query` / `require` /
`prefer`（executor プロパティプロトコルの三つ組）があります。欠けていると、
次のものが移植できません：`asio::query(strand, execution::context)`
（strand に下の context を尋ねる）、`require(strand, execution::blocking.never)`
（実行プロパティの設定）、および strand を executor コンセプトを満たすオブジェクト
としてジェネリックコードに渡す書き方全般。

## ch13 実験：read_with_future

10 行の実装の本質：promise と future の 1 回の受け渡しで、handler は promise の
`shared_ptr` を生かし続けます。asio が形式ごとに `read_with_xxx` を書かないのは、
`async_initiate` が「token をどう完了 handler にするか」を `async_result` の
特殊化に委ねているからです——発起の実装は 1 回だけ書かれ、新しい token 型は
自分の特殊化を連れてくれば済みます。

## ch13 バグハント：use_awaitable の再開者

`asio/use_awaitable.hpp` の特殊化では、完了 handler は awaitable frame への
参照を持つオブジェクトです。呼ばれると、結果を frame に書き込み、
`frame->resume()` を呼びます——coroutineは `co_await` の所から続きます。
sasio の対応物は `async_operation` コールバック内の `awaiting_.resume()`
（第 11 章のステップ ⑤）です。同名の兄弟の簡略版です。

# 付録 E：asio との意図的な差分一覧

章ごとにまとめます。正式なリストはこのリポジトリ README の `Deliberate limits`
セクションです（4 言語版を同期して維持）。この表は本書が展開した条目と、
asio のどこで見つけるかを集めたものです。

| 章 | 差異 | 理由 | asio の場所 |
| --- | --- | --- | --- |
| 全書 | Windows + WSAPoll のみ | 教学範囲 | `detail/` 下の 6 バックエンド |
| 全書 | executor / completion token なし | 合成層はエンジンではない | ch12–13 の対照表 |
| ch4 | handler は `std::function`（operation オブジェクトではない） | アロケーター/キャンセルフックを省く | `detail/scheduler_operation` |
| ch4 | タイマーとdescriptorは work 単位を保持しない | カウントはキューの handler だけ | `scheduler.ipp` の `outstanding_work_` |
| ch5 | interrupter は自己接続 UDP | ハンドルのペアを省く | `detail/select_reactor.hpp` の interrupter |
| ch6 | 1 タイマー 1 待ち | 待ちリストの維持を省く | `detail/timer_queue.hpp` の per_timer_data |
| ch6 | タイムアウトは scheduler が計算 | タイマーキューを独立した service に | `epoll_reactor` の `update_timeout` |
| ch7 | 方向ごとに 1 スロット（operation キューなし） | オブジェクト管理を省く | `reactive_socket_recv_op` |
| ch7 | socket は移動不可、accept は参照渡し | move 意味論を省く | `basic_stream_socket` |
| ch7 | オプションは `no_delay` のみ | 本書にはそれで十分 | `detail/socket_option*` の一族 |
| **ch9** | **UDP の切り詰めは `WSAEMSGSIZE` を飲まず、エラーとして届ける** | **POSIX に揃えるエラー吸収（吞错）を行っていない**（レビューで発見された挙動差） | `detail/impl/socket_ops.ipp` の `recvfrom` による `WSAEMSGSIZE` / `ERROR_MORE_DATA` の処理 |
| ch9 | resolver は問い合わせごとにスレッド、キャンセルは最新のみ | 最小限で理解できる版 | `detail/resolver_thread_pool.hpp` |
| ch9 | `udp::endpoint` が `tcp::endpoint` を再利用 | 型の体操を省く | `asio/ip/udp.hpp` |
| ch8 | 合成操作は単一バッファ、完了条件はハードコード | BufferSequence/ポリシー基底を省く | `impl/write.hpp` + `base_from_completion_cond.hpp` |
| ch8 | `async_read_until` なし | 動的バッファ+区切り検索は練習レベル | `asio/read_until.hpp` |
| ch10 | strand はオブジェクトで、executor アダプターではない | 適応できる executor 体系がない | `asio/strand.hpp` |
| ch10 | `bind_executor` の自動継承なし | 同上 | `asio/bind_executor.hpp` |
| ch11 | coroutineに token なし、完了は `~promise_type` で | `async_result` 体系を省く | `impl/awaitable.hpp`、`impl/co_spawn.hpp` |
| ch11 | `co_spawn` は直接 `resume()`（同期完了は run 外の可能性） | 設計どおりに提示、差異を登録 | `impl/co_spawn.hpp` の配送の開始 |

太字の行（ch9 の UDP 切り詰め）は、レビュープロセスが発見して登録した挙動の
差異で、README の `Deliberate limits` リストとこの表は同期しています。
