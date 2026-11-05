<!-- translated-from: book/src/ch08-composed.md sha256:33d0970b98ee -->

# 合成操作：operation の上に operation

第 7 章の echo サーバには、あまり見栄えのしないコードがありました——`session::do_write_some` です：

```cpp
void do_write(std::size_t length)
{
  write_size_ = length;
  write_offset_ = 0;
  do_write_some();
}

void do_write_some()
{
  auto self = shared_from_this();
  socket_.async_write_some(
      sasio::buffer(data_ + write_offset_, write_size_ - write_offset_),
      [this, self](error_code ec, std::size_t sent) {
        if (ec)
          return;
        write_offset_ += sent;
        if (write_offset_ < write_size_)
          do_write_some();       // not finished, continue
        else
          do_read();
      });
}
```

これが「バッファをすべて書き切る」の本当の代償です：オフセットの手動管理、再発起の手動処理、エラー処理——しかもこのロジックは、完全な書き込みが必要な**すべての**呼び出し点でコピーしなければなりません。書き込みでこれですから、読みはさらに悪い（TCP はバイトストリームで、1 回の `recv` がいくらくれるかはカーネルの気分次第——「N バイト読み切る」は必ずループになります）。この章は、この種のループを再利用可能な操作にliftします：`async_write(stream, buffer,handler)` と `async_read(stream, buffer, handler)`——発起は 1 回で、完了時には「バッファ全体が処理済み」（あるいは明確なエラー）が保証されます。新しいシステムコールは一切行いません。単に複数の `async_write_some` / `async_read_some` を 1 本のチェーンにつなぐだけ。だから合成操作（composed operations）と呼びます。

## 設計：リレーバトンに捕まった状態機械

合成操作の状態は驚くほど少ないです：

```cpp
template <typename Stream>
class write_op : public std::enable_shared_from_this<write_op<Stream>>
{
  Stream& stream_;            // target (a reference, not owned)
  const_buffer buffer_;       // the whole buffer to write
  std::function<void(error_code, std::size_t)> handler_;  // final completion
  std::size_t transferred_ = 0;   // bytes written so far
};
```

不変量は 2 つ：

1. **状態オブジェクトの生存期間＝操作チェーンの生存期間**。中間の完了 handler はどれも `shared_from_this()` をキャプチャします。チェーンがどこまで進んでも、状態はそこまで生きます。チェーンが終わる（最終完了が呼ばれる）と、最後のキャプチャが解放され、状態は自動的に破棄されます。リークなし、ダングリングなし、手動 delete もなし；
2. **各完了は 1 回だけ前進し、その後で次のセグメントを発起します**。`transferred_ += bytes` の後、次の `async_write_some` は残りの範囲だけを渡します（`buffer_.data() + transferred_`、長さ `buffer_.size() - transferred_`）。前進の終点は 4 つ：満杯（成功）、エラー、ゼロバイト（スピン防止の防御的停止）。

この形——**各完了が次の発起を作る**——が「合成」の全機構です。reactor にもキューにも触れず、コールバックのチェーンだけで単純な操作を複雑な操作に接ぎます。asio の `async_read` / `async_write` も同じで、違いは接着の方法が差し替え可能な完了条件になっていることだけです（対照表参照）。

## ある 100 バイトの書き込みの一生

送信バッファが 1 回に 60 バイトしか呑めないとします：

```text
async_write(s, 100B, done)
 └─ write_op constructed (transferred_=0)
     └─ s.async_write_some(buf[0..100))
         └─ poll reports writable → do_write → send writes 60
             └─ post(w1: transferred_=60, 60<100 → continue)
                 └─ s.async_write_some(buf[60..100))
                     └─ poll reports writable → do_write → send writes 40
                         └─ post(w2: transferred_=100 → complete(empty error))
                             └─ done(empty error, 100)
```

2 つの**匿名の中間 handler**（w1、w2）が現れます——lambda で、`self`（write_op）をキャプチャしています。ユーザーは 1 回 `async_write` を発起しただけですが、エンジンの中では本物の `send` が 2 回走り、中間の完了が 2 回 post されました。**最終 handler は w2 の呼び出しスタックの中で `complete()` が直接呼びます**——3 回目の入隊はありませんが、やはり run ループ内の実行なので、契約二は破られません。契約一もチェーン全体で果たされています：各中間 handler は `post` で届けられ、発起関数の中でインライン実行される一環はどこにもありません。

チェーンの終端にも注目：w2 が実行される時、`transferred_ == buffer_.size()` で、`do_write` は冒頭で完了を判定します——**最後の 1 周は新しい操作を発起しない**で、直接最終 handler を呼びます。中間 handler が `self` を順次解放し、w2 が終わって最後の参照を解放した瞬間、write_op は状態ごと破棄されます。不変量 1 の「チェーンの終わりまで生きる」は、このように自動的に果たされます。

## 精読：write.hpp のリレー核心

`include/sasio/write.hpp` の本文から（構築とメンバー宣言は省略）：

```cpp
void start() { do_write(); }

private:
void do_write()
{
  if (transferred_ == buffer_.size())       // full: done successfully
  {
    complete(error_code());
    return;
  }

  auto self = this->shared_from_this();     // ← the relay baton
  const char* data = static_cast<const char*>(buffer_.data()) + transferred_;

  stream_.async_write_some(
      const_buffer(data, buffer_.size() - transferred_),   // only the rest
      [self](error_code ec, std::size_t bytes) {
        self->transferred_ += bytes;

        if (ec)
        {
          self->complete(ec);               // error: finish with bytes written
          return;
        }

        // A zero-byte write would spin; treat it as completion.
        if (bytes == 0)
        {
          self->complete(error_code());     // zero bytes: defensive stop
          return;
        }

        self->do_write();                   // advance and re-initiate
      });
}

void complete(error_code ec)
{
  std::function<void(error_code, std::size_t)> handler = std::move(handler_);
  handler_ = nullptr;
  if (handler)
    handler(ec, transferred_);
}
```

4 つの細部、どれも見落としやすいものです：

- **`auto self = shared_from_this()` は発起の前にキャプチャします**。lambda は「次のセグメントを発起する」から「そのセグメントの完了が実行される」までの全窓にわたって参照を 1 つ持ちます。状態はこの間ずっと生きています。この行がなければ、中間の完了が実行される時に write_op はすでに破棄されているかもしれません——コールバックチェーンの最も古典的な use-after-free です；
- **`complete` は handler を move で引き出してから空にします**。最終 handler は正確に 1 回だけ呼ばれます——たとえ将来の編集が 2 つの完了経路を生んでも、2 回目は `if (handler)` で止まります。1 回限りの意味論は「覚える」ではなく「持ち去る」で保証します；
- **ゼロバイトの書き込みは終了条件であって、ループ条件ではありません**。`async_write_some` が 0 を返しても前進はゼロです。続けて再発起すれば永遠に 0——完了として扱う（書けたバイト数を添えて）、asio と同じ処理です；
- **エラー時の完了は `transferred_` を報告します**（0 ではなく）。「60 バイト書いた後で相手が切れた」完了は `(connection_reset, 60)` です——呼び出し側は、すでに起きた部分を知る必要があります。`async_read` も EOF で同じことをし、エラーは `sasio::eof` になります。

`async_write` 自体は薄い殻で、1 つの分岐が独立した注目に値します——**空のバッファ**です：

```cpp
if (buffer.size() == 0)
{
  stream.context().post([erased]() mutable {
    erased(error_code(), 0);
  });
  return;
}
```

何もせず、それでも `post` を通ります——直接 `handler(error_code(), 0)` とはしません。契約二のさらなる実践です：チェーンがゼロ仕事に退化しても、完了は `run()` の中で起きなければなりません。この分岐が `async_write(s, buffer(s, 0), h)` と任意の他の `async_write` の完了意味論を完全に同型に保ちます。

`read.hpp` は write と完全に対称で、2 つだけが違います：ゴールが「書き切る」から「読み切る」に変わり、**相手の切断（`async_read_some` が 0 バイトを返す）は `eof` エラーに昇格します**——バッファは永遠に満たされないので、「読み切る」という約束はもう果たせません。`async_read_some` との対比に注意：some 版にとって 0 バイトの戻りは「相手が切れた」の正常な報告ですが、合成版ではそれをエラーに昇格させます。約束が破られたからです。

## 対照表

| 問題 | asio | sasio |
| --- | --- | --- |
| 状態機械 | `impl/write.hpp` と `impl/read.hpp` の `detail::write_op` / `read_op` | `include/sasio/write.hpp` / `read.hpp` の同名クラス |
| 完了条件 | 差し替え可能：`transfer_all()`（デフォルト）、`transfer_exactly(n)`、`transfer_at_least(n)`、`detail/base_from_completion_cond.hpp` に宿る | ハードコードされた「すべて」 |
| バッファシーケンス | `ConstBufferSequence`（複数バッファ、scatter/gather） | 単一の `const_buffer` / `mutable_buffer` |
| EOF 意味論 | `async_read` は `error::eof` で完了 | 同じ（`sasio::eof`） |
| ストリーム変種 | `async_read_until`（動的バッファ + 区切り文字） | なし（差異表に登録） |

asio の完了条件は読む価値があります：`base_from_completion_cond` が「あとどれだけで完了か」をポリシー基底クラスにし、`write_op` は毎回の完了でそれに尋ねます。sasio は `transfer_all` をハードコードしました——汎用化を切り、幹を残すトレードオフの再現です。そして練習がそれを加え直します。

## 練習

**実験**：echo サーバを第 2 形態にアップグレードします——`session::do_write_some` の手書きループを丸ごと削除し、`do_read` の完了から呼びます：

```cpp
sasio::async_write(socket_, sasio::buffer(data_, length),
    [this, self](error_code ec, std::size_t)
    {
      if (ec)
        return;
      do_read();
    });
```

挙動は手書きループと完全に一致しますが、`write_offset_`、`write_size_` という 2 つのメンバーと `do_write_some` 全体が消えました——状態は `write_op` に移り、生存期間は shared_ptr のリレーが管理します。2 版を並べて答えてください：手書き版の `write_offset_` はどこにありましたか？合成版の対応する状態はどこにありますか？なぜ後者は session が持つ必要がないのか？

**バグハント + 拡張**：sasio に `transfer_at_least(n)` を追加します——**少なくとも** n バイト読み/書きしたら完了とします（asio の同名完了条件）。ヒント：`read_op` / `write_op` に `std::size_t least_` メンバーとコンストラクタ引数を追加し、2 か所の完了判定を `transferred_ == buffer_.size()` から `transferred_ >= least_`（または合成形）に変えます；`async_read` / `async_write` に `least` を取るオーバーロードを追加します。終わったら問います：`transfer_at_least(0)` は現在のデフォルト挙動とちょうど等しいですか？0 バイトの `async_read_some` と `eof` の組み合わせで、`least_ = 0` はエラーか成功か？asio と揃えるには、判定順序を `base_from_completion_cond.hpp` で確認してください——実際の順序は `impl/read.hpp` にあります。完了条件は I/O の前にチェックされます。

---

1 回の読み、1 回の書き、UDP と名前解決が揃えば、使えるネットワークライブラリの API 面は完成です。次章は2 つのコンポーネントを追加します：UDP の `send_to` / `receive_from`（同一 reactor の 2 つ目の使い方）、そして「ドメイン名をアドレスに」をワーカースレッドに投げる resolver（ライブラリ唯一のスレッドを持つコンポーネント）。
