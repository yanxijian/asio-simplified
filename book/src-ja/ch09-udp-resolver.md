<!-- translated-from: book/src/ch09-udp-resolver.md sha256:a27814c9337f -->

# UDP とresolver：同一エンジンの 2 つの再利用

エンジンの三大部品が揃い、TCP の 2 相式が立ち上がったので、使えるネットワーク
ライブラリの骨格はすでに完成しています。本章は日々の用途のための最後の 2 ピースを
はめます。それぞれに教学の価値があります：

**UDP** はシステムコールを替えます（`sendto` / `recvfrom`）が、**エンジンには
ほとんど触れません**——同じdescriptorテーブル、同じ `WSAPoll`、同じ
「登録、準備完了、実行、post」。読み終えれば確認できるはずです：第 5 章の
reactorは設計の時点からプロトコルにとらわれない作りでした。

**resolver**（名前解決）は正反対です：`getaddrinfo` はブロッキング呼び出しで、
descriptorもなければ準備完了もありません——reactor のモデルでは覆えません。
asio の答えはスレッドプール。sasio の答えは**問い合わせごとに 1 つの短命スレッド**。
ライブラリで唯一スレッドを導入するコンポーネントであり、「work 単位が `run()` を
生かし続ける」の最良の教材です。

## UDP：システムコールを替えるだけ

`include/sasio/ip/udp.hpp` の socket を、TCP 版とフィールドごとに比べます：

```cpp
bool send_pending_ = false;
std::function<void(error_code, std::size_t)> send_handler_;
const_buffer send_buffer_;
endpoint send_destination_;              // the extra one: where to

bool receive_pending_ = false;
std::function<void(error_code, std::size_t)> receive_handler_;
mutable_buffer receive_buffer_;
endpoint* receive_sender_ = nullptr;     // the extra one: write back who sent it
```

非同期の入口は 4 つだけ：`async_send_to(buffer, destination, handler)` と
`async_receive_from(buffer, sender, handler)`（コネクションレスのプロトコルには
「接続」の概念がなく、送受信のたびに相手のアドレスが付きます。だから connect も
accept もなく、スロットの組は 3 組から 2 組に減ります）。

2 相式の形は TCP と完全に同型です。受信を例に：

- **発起** `start_receive_from`：バッファを保存、`sender` ポインタを保存、
  読み準備を登録、返る；
- **準備完了** `do_receive`：1 回の `recvfrom` で完全なデータグラムを受け取り、
  送信元アドレスを `from_sockaddr` 経由で `*receive_sender_` に書き戻し、
  `complete_receive` に渡します；
- **配送**：handler は `post` で出て行き、バイト数と送信元エンドポイントを
  携えています。

エラー経路も TCP をそのまま写します：偽警報では再登録、本物のエラーでは
`complete_receive(ec, 0)`、`close()` は保留中の方向にまず
`complete(operation_aborted)`。第 7 章のすべての結論は UDP の上でそのまま
成立し、例外は 1 つもありません——これこそreactor抽象の価値です：
**プロトコルが変わっても、エンジンは変わらない**。

UDP 特有の注意点が 2 つ：

- **`receive_sender_` は生ポインタです**。保留中は有効な `endpoint` を指して
  いなければなりません——「バッファは生かしておく」と同じユーザー責任です。
  データグラムが届いた時に書き戻すのは「このデータグラムがどこから来たか」で
  あって、「この socket が誰とつながっているか」ではありません（UDP に
  接続という概念はありません）；
- **データグラムには境界があります**。1 回の `recvfrom` は 1 つの完全な
  データグラムを受け取ります。バッファが小さければ切り詰められます（asio の
  挙動は `socket_ops.ipp` の `WSAEMSGSIZE` 処理を通じてデータグラムをエラーに
  して捨てます。sasio はエラーを handler に届けます——付録 E 参照）。TCP の
  「N バイト読み切る」ロジックはここでは通用しません——ストリームがないので、
  合成操作の出番もありません。

## resolver：ブロッキング呼び出しの非同期化

`getaddrinfo("example.com", "80")` は数百ミリ秒から数秒かかることがあり
（DNS を引きに行く）、その間スレッド全体が止まります。run ループに放り込めば
すべての handler が足止めされ、reactor に入れても解決しません——descriptorが
ないのです。唯一の出路：**別のスレッドに投げて、自分は走り続ける**。

sasio の `resolver::start_resolve` 全文（ライブラリで唯一のスレッドコードです。
まるごと読む価値があります）：

```cpp
void resolver::start_resolve(const query& q,
    std::function<void(error_code, results)> handler)
{
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_flag_ = cancelled;               // cancel() touches the latest query only
  }

  scheduler* sched = &scheduler_;
  scheduler_.on_work_started();             // ← work unit #1

  try
  {
    std::thread([sched, q, cancelled, handler = std::move(handler)]() mutable {
      error_code ec;
      results resolved = do_resolve(q, ec); // blocking getaddrinfo happens here

      // Decide cancellation once, when the lookup has finished. A cancel()
      // that arrives after this point no longer changes the result, the same
      // way asio leaves an already-completed operation untouched.
      const bool aborted = cancelled->load(std::memory_order_acquire);

      sched->post(
          [sched, handler = std::move(handler), ec,
              resolved = std::move(resolved), aborted]() mutable {
            struct release
            {
              scheduler* target;
              ~release() { target->on_work_finished(); }   // ← the unit returns
            } guard = {sched};

            handler(aborted ? operation_aborted : ec, resolved);
          });
    }).detach();
  }
  catch (...)
  {
    scheduler_.on_work_finished();          // no thread, the unit returns now
    throw;
  }
}
```

5 つの要点、時間順に：

1. **work 単位はスレッドの前に成立します**。`on_work_started()` は
   `std::thread` の構築の**前**に呼ばれます——この瞬間から、四条件の
   `work_count_ != 0` が成立し、`run()` は「キュー空、タイマーなし、
   descriptorなし」で止まりません。これがなければ、スレッドはまだ解析中なのに
   run ループが抜けて context が破棄され、戻ってきた `post` は宙に浮く
   ポインタに触れることになります；
2. **スレッドは detach です**。キャプチャするのは値だけ（scheduler ポインタ、
   query、フラグ、handler）で、join する人は必要ありません。代償は 1 つの
   硬い制約：**io_context は未完了の `async_resolve` より長く生きていなければ
   なりません**——スレッド内の `sched->post(...)` が参照する scheduler ポインタは、
   context が破棄された後には宙に浮きます；
3. **キャンセルは撤回ではなくサンプリングです**。`cancel()` は最近の問い合わせの
   フラグを true に裏返すだけ。スレッド内の `getaddrinfo` は中断できず、自分で
   返るのを待つしかありません。`aborted` は**スレッド内で、検索が終わった後**に
   1 回だけサンプルされます——この瞬間以降の `cancel()` は結果を変えません。
   これは asio の「すでに完了した操作はキャンセルの影響を受けない」と一致します。
   サンプリング位置こそが意味論そのものです；
4. **結果は `post` で run ループに戻ります**。スレッドが handler を直接呼ぶことは
   絶対にありません——スレッドの持っている結果（エラーコード + エンドポイントの
   リスト）は値として lambda にキャプチャされ、キューに投げられます；
5. **work は post された lambda の上で返ります**。`release` ガードのデストラクタは
   handler の実行の**後**に発火します——解析中も（handler がキューで待っている間も
   含めて）work 単位は帳簿に残り、handler が走り終わって初めて解放されます。
   例外でも漏れません。RAII の古典技で、第 4 章 `handler_work` の
   クロススレッド版です。

同期版の `resolve()` は同じ `do_resolve` を共有し、失敗時には
`std::system_error` を投げます（エラーコードは独立した resolver カテゴリに乗り、
メッセージは `gai_strerror` から来ます）。教学の便宜のためのもので、本物の
コードでは非同期版を使ってください。

## 対照表

| 問題 | asio | sasio |
| --- | --- | --- |
| 解析の実行者 | 常駐スレッドプール（`detail/resolver_thread_pool`） | 問い合わせごとに短命の detach スレッド |
| キャンセル | 完全なキャンセルキュー、問い合わせごと | サンプルフラグ、最新の問い合わせのみ |
| 結果 | `results`（反復可能、サービスのヒント付きで順序付き） | `std::vector<endpoint>`（IPv4） |
| スレッドの生存期間 | プールは context と運命を共にする | detach、work 単位 + ユーザーの生存保証に依存 |
| endpoint 型 | `udp::endpoint` / `tcp::endpoint` が別々 | 同じ `endpoint` を再利用 |
| datagram socket | `basic_datagram_socket.hpp` | `src/ip_udp.cpp` |

`udp::endpoint` の再利用に独立した注を 1 つ：asio では 2 つの別々の型です
（どちらも IPv4 アドレス/ポートの組であるにもかかわらず）。「TCP のエンドポイントを
UDP socket に送る」ような間違いをコンパイル時に防ぎます。sasio は 1 つに
まとめました——型が減り、チェックは実行時まで延期されます。「教学版が型の
体操を切る」また 1 例です。切られたものには名前がついていて、差異表に
登録されています。

「問い合わせごとに 1 スレッド」も同じ種類のトレードです。問い合わせが多い時は、
スレッド作成のオーバーヘッドもキャンセルできない `getaddrinfo` も本物のコストで、
asio の常駐プール + キャンセルキューが生産レベルの答えです。sasio が与えるのは
**最小限で理解できる**バージョン——スレッド、work、post の 3 つの概念が
それぞれ一度だけ現れ、一読で透けて見えます。

## 練習

**実験**：UDP echo サーバを 30 行以内で書きます：

```cpp
sasio::io_context ctx;
sasio::ip::udp::socket sock(ctx);
sock.open();
sock.bind(sasio::ip::udp::endpoint(5555));        // same port as ex03

char recv_data[1500];                              // one buffer for receiving
sasio::ip::udp::endpoint sender;

std::function<void()> do_receive = [&]() {
  sock.async_receive_from(sasio::buffer(recv_data), sender,
      [&](sasio::error_code ec, std::size_t n) {
        if (!ec)
        {
          auto payload = std::make_shared<std::vector<char>>(
              recv_data, recv_data + n);           // each send gets its own copy
          sock.async_send_to(sasio::buffer(*payload), sender,
              [payload](sasio::error_code, std::size_t) {});
        }
        do_receive();                              // keep receiving
      });
};
do_receive();
ctx.run();
```

`nc -u 127.0.0.1 5555` で数行叩いてエコーを確認します。あの `payload` に注目：
受信と送信に 1 つのバッファでは足りません——`async_send_to` がまだ終わっていないのに、
次の `async_receive_from` はすでに同じバッファで登録済みで、2 つ目のデータグラムが
送信中の内容を上書きします。送信ごとに自分のコピーを作り、lambda が `shared_ptr`
で生かし続け、送信の完了（handler の破棄）で解放します。この手が見覚えがありますか？
第 8 章 `write_op` のリレー バトン、ポケット版です。

**バグハント**：2 つのタイミング——`aborted` はそれぞれ何になりますか？

- タイミング A：`async_resolve` を発起した → スレッドはまだ `getaddrinfo` の中 →
  `cancel()` → 検索が終わる → サンプリング；
- タイミング B：`async_resolve` を発起した → 検索が終わる → **サンプリングの後** →
  `cancel()` → handler が実行される。

`start_resolve` のコードに対して 1 行ずつ確認してください：A は
`operation_aborted` を届け、B は解析結果を届けます。さらに一歩：サンプリングが
post の後の handler の中（スレッドではなく）で行われていたら、タイミング A の
キャンセル通知はどれだけ遅れるでしょうか？——「サンプリング位置こそが意味論」
という結論は、コードの中から自分で生えてくるはずです。

---

TCP、UDP、名前解決、合成操作——使えるネットワークライブラリの API 面が
完成しました。しかしマルチスレッドの `run()` の下では、2 つの handler が同じ
`session` を同時に触る場面は、遅かれ早かれ必ず来ます。次章の strand：1 つの
キュー、1 つの running フラグが、「非並行」を組み合わせ可能な保証に変えます。
