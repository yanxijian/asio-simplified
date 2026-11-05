<!-- translated-from: book/src/ch07-socket.md sha256:bd7c0e04fd3b -->

# socket：発起関数は「準備完了後に何をするか」を記述するだけ

エンジンの 3 章が道具をすべて揃えました：キューが順番を管理し、reactor が準備完了を管理し、timer heapが時間を管理します。この章から、このエンジンを実際に使います。まず `src/ip_tcp.cpp` の本物のコード、`async_read_some` の発起関数を見てください：

```cpp
void socket::start_read_some(mutable_buffer buffer,
    std::function<void(error_code, std::size_t)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor, 0);
    });
    return;
  }

  if (read_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started, 0);
    });
    return;
  }

  read_pending_ = true;
  read_buffer_ = buffer;
  read_handler_ = std::move(handler);
  reactor_.start_read(handle_, [this]() { do_read(); });
}
```

3 回読んでも、`recv` は見つかりません。この関数がしたのは、正確に 4 つのことだけです：状態を検査し、バッファと handler を**メンバーに保存**し、「読み可能になったら呼んで」という関心を 1 つ登録し、返る。本物の `recv` は別の関数 `do_read` の中にあり、reactor が準備完了を報告した時にのみ呼ばれます。

本章は「発起」と「完了」の 2 相をどちらも開きます。これは asio のすべての socket コードの共通の形であり、`basic_stream_socket` のすべての `async_*` がこの形をしています。

## 設計：1 方向 1 スロット

socket の状態は多くありませんが、各メンバーにそれぞれ理由があります：

```cpp
SOCKET handle_ = INVALID_SOCKET;

bool read_pending_ = false;
std::function<void(error_code, std::size_t)> read_handler_;
mutable_buffer read_buffer_;
// a symmetric set for the write direction
```

不変量は 3 つ：

1. **1 方向につき、未完了の操作は高々 1 つ**。`read_pending_` がスロットの占有印です——2 回目の読み発起は、`post` で届く `already_started` を受け取ります（注：依然として post であり、エラー経路でも契約一は破られません）。asio は operation キューで任意の数を支えますが、sasio は 1 スロットです——容量の差であって、形の差ではありません；
2. **read/write/accept の発起関数はデータ面のシステムコールを行いません**；connect が例外です——発起時に 1 回の非ブロッキング connect を試みます（connect には「準備完了」がありません：即座に成功、即座に失敗、進行中の 3 段階があり、3 番目だけが書き準備の登録を必要とします）；
3. **完了の配送はすべて `post` を経由します**、即座の失敗も含めて——`bad_descriptor` はその場で投げられるのではなく、キューに入って届けられ、handler が常に `run()` の中で実行されることを保証します（契約二）。

もう 1 つ、必ず名前を上げるべき境界があります：**バッファと handler の生存期間は発起側の責任です**。`read_buffer_` が保存するのはポインタと長さであって、データのコピーではありません——操作が保留されている間、呼び出し側はバッファを生かしておかなければなりません。asio も同じで、これが C++ 非同期 I/O の標準的な約束です。

## ある読み操作の 2 相式

第 2 章のシーケンス図をコードに落とします。**第 1 相（発起）**は上の `start_read_some` で、`reactor_.start_read(handle_, [this]() { do_read(); })` で終わります。

**第 2 相（準備完了）**は、reactor が `WSAPoll` から戻った後に呼び出します：

```cpp
void socket::do_read()
{
  if (!read_pending_)
    return;

  error_code ec;
  const int result = detail::socket_ops::recv_socket(handle_,
      read_buffer_.data(), clamp_to_int(read_buffer_.size()), ec);

  if (result < 0)
  {
    if (ec == would_block)
    {
      // Spurious readiness: register again and wait.
      reactor_.start_read(handle_, [this]() { do_read(); });
      return;
    }

    complete_read(ec, 0);
    return;
  }

  complete_read(error_code(), static_cast<std::size_t>(result));
}
```

ここが `recv` の実体です。3 つの出口：

- **偽警報**：poll は読めると言ったのに、`recv` が `would_block` を返す（チェックと呼び出しの間にデータが消えた、または poll のレベルトリガの意味論自体が少し曖昧）。対応は登録し直して待ちに戻ること——第 5 章の不変量 2（「移し出してから実行」）が、明示的な再登録を必須にしています。そうしなければ、この読み操作は reactor の世界から消えてしまいます；
- **本物のエラー**：`complete_read(ec, 0)`——占有印をクリア、関心を登録解除、handler をエラーコードとともに `post` します；
- **成功**：`complete_read(空のエラーコード, n)`、同じ道です。

`complete_read` 自体は十余行：`read_pending_` をクリア、reactor の関心を登録解除、handler をメンバーから move で引き出し、`post` します。スロットが空になることは、**スロットが次の発起ですぐに再利用できる**ことを意味します——前の handler がまだキューの中で実行を待っていても、新しい `async_read_some` はすでに合法です。handler の一生とスロットの一生は、ここで切り離されます。

## 2 つの非対称：connect と close

**connect には同期の終わりがなく、3 状態があります**。発起関数 `start_connect` は非ブロッキングの `connect` を直接呼びます：

- 即座に成功（ループバックではよくある）：`complete_connect(空のエラーコード)`；
- 即座に失敗（到達不能など）：`complete_connect(ec)`；
- `would_block`（接続進行中）：書き準備の関心を登録し、カーネルが 3 ウェイハンドシェイクを終えるのを待ちます。

第 3 の準備完了の後、`do_connect` はデータを 1 バイトも書きません。するのは 1 つのこと——`getsockopt(SO_ERROR)` で接続の最終結果を読み取り、`complete_connect` に渡します。**ここでの「書き可能」の意味は「ハンドシェイクが終わったか失敗したか」であり、結果は SO_ERROR から受け取ります**。これが非ブロッキング connect の標準的な作法で、asio の `detail/reactive_socket_connect_op` も同じ流れです。

**close は完了を注入するのであって、撤回するのではありません**。`socket::close()` はまず `cancel()`：保留中の各方向に `complete_xxx(operation_aborted)` を呼び——handler は `post` され、「操作がキャンセルされた」エラーコードを携えて届きます。その後でようやくdescriptorの登録を解除し、ハンドルを閉じます。つまり close の後、あなたは保留中のすべての操作の完了通知を**必ず**受け取ります。ただしエラーコードが `operation_aborted` なだけです。第 6 章タイマーの「キャンセルは通知なしを意味しない」が socket 上でそのまま再演され、理由も同じです：`close()` を呼ぶスタックは、handler が触りたいスタックそのものかもしれず、その場で呼ぶことは再入になります。

acceptor は socket の特殊形です：`async_accept` は読み準備を登録し（新しい接続の到着 = リスニング socket が読める）、`do_accept` の中で `accept()` が新しいハンドルを受け取り、非ブロッキングに設定し、`peer->assign()` で呼び出し側が渡した socket オブジェクトに引き渡します。読みと完全に同型で、これ以上展開しません。

## 精読：cancel と complete_read

「キャンセル」の 2 つのコードをつなげて読みます。socket の一生の締めくくりの動作です：

```cpp
void socket::cancel()
{
  if (!is_open())
    return;

  if (connect_pending_)
    complete_connect(operation_aborted);
  if (read_pending_)
    complete_read(operation_aborted, 0);
  if (write_pending_)
    complete_write(operation_aborted, 0);
}

void socket::complete_read(error_code ec, std::size_t bytes)
{
  read_pending_ = false;
  reactor_.stop_read(handle_);

  std::function<void(error_code, std::size_t)> handler =
      std::move(read_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec, bytes]() mutable {
    handler(ec, bytes);
  });
}
```

3 つの細部：

- `complete_xxx` は冪等な締めの入り口です：フラグをクリア、登録を解除、**handler をメンバーから move で引き出し**、post。この後、socket のメンバーにはこの操作の痕跡が残らず、デストラクタも、close も、次の発起も安全です；
- post の lambda は **handler と結果を値でキャプチャ**します——socket のどのメンバーも参照しません。これこそ、close が complete の直後にハンドルを閉じられる理由です。投げ出された通知は自己完結しているからです；
- `if (!handler) return;` は「空のスロットがキャンセルされた」という退化ケースの防御です——正常な経路ではここに届きません。

asio との対照：`basic_socket::cancel` は `reactor.cancel_ops(descriptor)` を通って、reactor がそのdescriptorの operation キュー全体を一つずつ `operation_aborted` にして投げます。機構は違います（キュー vs スロット）、挙動は同じです（完了は必ず届き、決して再入しない）。

## 対照表

| 問題 | asio | sasio |
| --- | --- | --- |
| 発起関数 | `basic_stream_socket.hpp` の `async_read_some` | `src/ip_tcp.cpp` の `start_read_some` |
| 完了の担い手 | `detail/reactive_socket_recv_op.hpp`（operation オブジェクト：handler/アロケーター/work 単位） | メンバースロット + handler をキャプチャした `post` lambda |
| 保留数 | 方向ごとに 1 キュー | 方向ごとに 1 スロット |
| connect | `reactive_socket_connect_op` + SO_ERROR | `start_connect` / `do_connect`、同じ流れ |
| キャンセル | `reactor.cancel_ops`、op ごとに投げる | `cancel()`、スロットごとに投げる |
| ハンドル層 | `detail/socket_ops.hpp` + `impl/socket_ops.ipp`（5,140 行） | `src/socket_ops.cpp` + ヘッダー（259 行） |

asio の operation オブジェクトにはもう一言：それは「イベントが起きるまで生きられる状態の運搬体」で、handler、アロケーター、work 単位、完了結果を 1 つのヒープ上のオブジェクトにまとめています。sasio はそれをメンバースロットで置き換えました——代償は方向ごとに 1 つの保留（不変量 1）、利益は本章が 2 相式そのものに集中できること。第 8 章の合成操作は「チェーンの終わりまで生きる状態」をまた必要とします。その時、shared_ptr のリレーを見てください。

## 練習

**実験**：ch2 の echo サーバを socket レベルに落とします。`examples/ex03_echo_server.cpp` の `session::do_read` / `do_write_some` をもとに、答えて検証してください：2 つの接続が同時に 1KB ずつ送った時、シングルスレッドの `run()` の中で、この 2 つの session の `do_read` は交差実行されますか？2 つの handler にマークと時刻を印刷して検証してください。（予測：交差します——各 session の `async_read_some` の完了は独立したキュー項目で、準備ができたものから処理されます。）

**バグハント**：`socket::close()` の後、投げられた `operation_aborted` handler が実行される前に、ユーザーがすぐに同じ socket オブジェクトに新しい `async_read_some` を発起しました。`handle_` はすでに `INVALID_SOCKET` です——`start_read_some` はどの分岐を通りますか？handler はどんなエラーコードを受け取りますか？2 つの通知（古い aborted、新しい bad_descriptor）の順序は保証されていますか？まずコードから答えを導き、20 行のプログラムで検証してください。

---

1 回の `async_read_some` が返してくるのは、たいてい一部分です——TCP はバイトストリームであり、1 回の `recv` がいくらくれるかはカーネルの気分次第です。「満たされるまで read_some を繰り返す」を、自分でリレーできる汎用の操作にしたのが、次章の合成操作 `async_read` です。
