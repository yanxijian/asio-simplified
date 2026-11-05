<!-- translated-from: book/src/ch05-reactor.md sha256:35a45ce2f608 -->

# reactor：準備完了・起床・descriptorテーブル

第 4 章のループに、ずっと開かないままの 1 行がありました：

```cpp
reactor_.run_for(timeout);   // the only place that ever blocks
```

`run()` の時間のほとんどはこの行の上で眠っています。何の上で眠り、何で目を
覚ますのか？第 2 章のシーケンス図は「WSAPoll が準備完了を報告する」と言いましたが、
「報告」の裏にはもう 1 つ、はっきりさせるべき問題があります——**眠っている人は
どうやって起こされるのか？** この章は `src/reactor.cpp` を分解します。そこには
全書で最も見応えのある 2 つのバグがあります。私たちが作った教育用のケースではなく、
このリポジトリのコミット履歴で実際に起き、テストに捕まったものです。

## 設計：1 つの表と、突くと目覚める管

reactor に関係あるメンバーは 3 つだけです：

```cpp
mutable std::mutex mutex_;
std::unordered_map<SOCKET, descriptor_state> descriptors_;  // the table
SOCKET interrupter_ = INVALID_SOCKET;                        // self-connected UDP
std::atomic<bool> interrupted_{false};                       // dedup flag
```

**descriptorテーブル**は reactor の名簿：socket ごとに 1 行、「誰が読みを
待ち、誰が書きを待つか」を記録します：

```cpp
struct descriptor_state
{
  std::function<void()> read_handler;    // invoked on read-ready (it is do_read)
  std::function<void()> write_handler;   // invoked on write-ready (it is do_write)
  bool empty() const { return !read_handler && !write_handler; }
};
```

ここに保存されているのは socket の**内部コールバック**
（`[this]() { do_read(); }`）であって、ユーザーの handler ではありません——
ユーザーの handler は、`do_read` が本物のシステムコールを終え結果を得た後、
`post` でキューに入ります（第 2 章の契約一）。1 方向に同時に 1 つの登録だけを
許すのは、socket の「方向ごとに 1 操作」ルールと呼応します。

**interrupter** は警棒です。自分のアドレスに `connect` した UDP socket で——
中に 1 バイト書き込むと、それ自身が即座に「読み可能」になります。これを
`WSAPoll` の監視リストに混ぜておけば、眠っている poller を起こしたいスレッドは
1 バイト書けばいい。asio は POSIX で socket pair、eventfd 対応の Linux では
eventfd、Windows では socket pair を使います——sasio は自己接続 UDP で
ペアのハンドルを省き、効果は同じです。

不変量を 2 つ、ここに置き、本章の後半で何度も戻ってきます：

1. **poll 集合はスナップショットです**。`WSAPoll` が見るdescriptorのリストは、
   呼んだ瞬間にテーブルから写したものです。写した後に登録された新しい
   descriptorは、この周では見えません。だから**待ち集合を変える新しい登録は、
   眠っているスレッドを必ず起こさなければなりません**。再写させるために；
2. **準備完了の登録は、表から出してから実行します**。`WSAPoll` はレベルトリガ
   （水平トリガ）です。データが読まれない限り、次の poll は同じdescriptorを
   また報告します。だから準備完了のコールバックは、表から移し出してから実行
   します——`do_read` の `recv` が偽警報（spurious readiness）で何も読めなかったら、
   登録し直して待ちに戻ります。

## ある起床の一生

3 つの起床経路で最もよく使われるのが `post` です。`scheduler::post` から眠っている
スレッドが目を覚ますまで、5 ステップ：

1. スレッド B が handler をキューに積み、ロックを出て、`reactor_.interrupt()` を
   呼びます；
2. `interrupt()` はまず原子フラグ `interrupted_` を見ます：すでに true なら、
   誰かがついさっき突いたばかりでバイトがまだ飛んでいる途中なので、そのまま
   返ります（**重複の排除**——千回の post は 1 バイトに潰されます）；
3. フラグが false→true に裏返り、interrupter に 1 バイトを `send` します；
4. スレッド A は `WSAPoll` の中で眠っていて、poll リストの最後の項目が
   interrupter なので、バイトが届いた瞬間に戻ります；
5. A は準備完了のdescriptorを処理した後、interrupter のバッファを**読み尽くし**
   （`recv` ループで空になるまで）、フラグを false に戻します——次の周はまた眠り、
   次の突きを待ちます。

重複排除フラグはこの機構の妙味です：起床は**レベル**の意味論（バイトがあれば
読める）であり、`post` は**エッジ**のイベント（handler ごとに 1 回突く）。フラグが
なければ、poll の読み尽くしと `interrupt` の書き込みが互いに競走します。フラグが
あれば、複数の post は 1 回の起床に潰れます——目覚めた後、ループの頂上はキューを
**丸ごと**見直すので、1 回突いても 10 回突いても同じです。

## ケーススタディ：2 回の起床漏れ、1 回の生まれつきの正しさ

3 つの起床経路（post、初回descriptor登録、新しい最早タイマー）の来歴は
同じではありません。1 つは初版から正しく、2 つはテストに捕まりました。この対照を
理解すれば、「スナップショットモデル」の代償の全体が見えます。

**生まれつき正しい：post。** `scheduler::post` は初版から条件なしで
`reactor_.interrupt()` を呼んでいました——キューが変わったら、眠っているスレッドは
目を覚まして見なければなりません。この行が欠けたことは一度もありません。キューと
同じ関数の中にあり、目に見える形で結合しているからです。教えるのは：**状態を
変える場所で、その場で起こす**。

**1 回目の失守：初回descriptor登録。** 第 2 章が reactor にdescriptor表を
追加した時、`start_read` はこうでした：

```cpp
void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  std::lock_guard<std::mutex> lock(mutex_);
  descriptors_[descriptor].read_handler = std::move(handler);
  // no wake-up. The sleeping thread knows nothing of this.
}
```

シングルスレッドなら完全に問題ありません——`async_read_some` を発起するスレッドは
run ループ自身で、登録し終えたら次の周で自然にスナップショットを再取得します。
マルチスレッドで足が露になります：スレッド A が `WSAPoll` で眠り（タイマーなし、
タイムアウト無限）、スレッド B が新しい socket で `async_read_some` を発起する——
登録は表に入りましたが、A の手のスナップショットは古いまま。タイマーも post も
救いに来ません。**この読み操作は世界の終わりまで待ちます**。

asio の `select_reactor` は答えを `start_unready_op` に書いています：

```cpp
bool first = op_queue_[op_type].enqueue_operation(descriptor, op);
scheduler_.work_started();
if (first)
  interrupter_.interrupt();     // ← what sasio was missing
```

sasio の修復（`src/reactor.cpp`）はこの形をそのまま写します：

```cpp
void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  bool first = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_state& state = descriptors_[descriptor];
    first = !state.read_handler;      // empty→non-empty in this direction = first
    state.read_handler = std::move(handler);
  }

  // A thread already blocked in WSAPoll() is waiting on a snapshot of the
  // descriptor set taken before this registration, so it must be woken up to
  // rebuild that set. asio does exactly this in
  // select_reactor::start_unready_op():
  //   if (first) interrupter_.interrupt();
  if (first)
    interrupt();
}
```

`first` の判定はロック内で行い（その方向の登録が空からあり）、`interrupt()` はロックの外で
呼びます——順序を逆にすると、ロックを持ったままシステムコールをすることになり、
必要はありません。`start_write` も同様です。回帰テスト
`tcp_multithread_wakeup_test` の判別方法は直接的です：1 つのスレッドが
`WSAPoll` で眠り、別のスレッドが読みを登録して対端からデータを書く——handler が
3 秒以内に実行されることをアサートします。起床がなければ、唯一の目覚め源は
5 秒後のウォッチドッグタイマーで、テストは確実に失敗します。

**2 回目の失守：新しい最早タイマー。** descriptorを直しても、同族の問題が
もう 1 つ残っていました。第 4 章の睡眠時間の公式は「次のタイマーの期限 - now」
——このタイムアウトも**スナップショット**です：計算した後に、もっと早いタイマーが
別のスレッドから加わっても、眠っているスレッドは旧の時刻に従って目を覚まします。
さらに悪い変体：無限に眠ると計算された（当時タイマーなし）後、別のスレッドが
100ms の `async_wait` を入れる——誰も起こさなければ、眠りに終わりがありません。

asio はここにも答えを持っています。`select_reactor::schedule_timer`：

```cpp
bool earliest = queue.enqueue_timer(time, timer, op);
if (earliest)
  interrupter_.interrupt();     // new earliest timer → wake and recompute
```

sasio の修復は `timer_queue::add` に「新しいヒープ頂点になったか」を返させ、
`steady_timer::async_wait` がそれに応じて `scheduler::wake_waiters()` を呼ぶ
（中身は `reactor_.interrupt()`）。回帰テスト `timer_multithread_wakeup_test`：
run スレッドを先に眠らせ（タイマーなし）、メインスレッドが 100ms の待ちを
入れる——修復前は handler は永遠に実行されず、テストは `fired` フラグで失敗に
なります（1 秒後メインスレッドが `stop()` で収める）。

**3 つの対照、1 つのパターン。** post の起床は「状態を変える場所で、その場で
起こす」；2 回の失守はどちらも「新しい待ちの源」の入口（新しいdescriptor、
新しいタイマー）で、修復はどちらも「新しい最早の待ちになる → 起こす」でした。
突き詰めれば、スナップショットモデルの総綱は 1 文：**`WSAPoll` の答えを変える
可能性のある操作は、すべてその答えを待っている人を起こさなければならない**。
asio の `epoll_reactor` に最初の失守はありません——epoll のdescriptor集合は
カーネルの内にあり、`epoll_wait` は新しい登録を自然に見えます。スナップショットの
問題はそもそも存在しない。しかしタイマーの失守は、timerfd なしの epoll でも
存在します（`update_timeout()` → `interrupt()`）。プラットフォームの機構は違い、
スナップショットの落とし穴は共有されます。

## 精読：run_for の 4 楽章

起床の受け手は `run_for` です。全文約 100 行、骨格は 4 段：

```cpp
void reactor::run_for(clock::duration timeout)
{
  std::vector<WSAPOLLFD> poll_fds;        // ① copy the snapshot under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (each row of the descriptor table)
      if (read or write registered)
        { poll_fds.push_back(fd); remember socket; }
    poll_fds.push_back(interrupter);      // the truncheon rides last
  }                                       // ② lock released — poll without it

  const int result = ::WSAPoll(poll_fds.data(), ..., to_milliseconds(timeout));

  std::vector<std::function<void()>> ready_handlers;   // ③ move out under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (each ready descriptor)
      move that direction's callback out of the table into ready_handlers;
  }
  for (auto& handler : ready_handlers)    // ④ execute without the lock
    handler();

  if (interrupter readable) { drain buffer; clear flag; }  // ⑤ tidy up
}
```

4 つの細部、それぞれに 1 段ずつ：

- **② poll の間はロックなし**。`WSAPoll` は無限に眠れます。reactor のロックを
  握ったまま眠ると、他のスレッドの `start_read` が全部詰まります——デッドロックの
  典型レシピ（ロックを必要とする人はあなたを起こそうとし、あなたはロックを握って
  起こされるのを待つ）。スナップショットはロック内で写し終え、poll はロックなしで
  行う。両者の間の時間窓こそ「起床が必要」な理由です；
- **③ 移し出してから実行**。レベルトリガの poll はバッファにデータがあるかだけを
  見ます。準備完了のコールバックが表に残り、`recv` が一歩遅れて実行されたら、
  次の poll はまた同じdescriptorを報告します。コールバックを表から移し出す
  （不変量 2）ことで、偽警報も二重配送もこの動作が防ぎます——`do_read` が本当に
  `would_block` を読んだら、自分で登録し直します；
- **④ ロックなしで実行**。scheduler の「ロック内でデキュー、ロック外で実行」と
  同じ鉄則：コールバックは再登録をし、post をします——すべてロックを必要とする
  可能性があります；
- **⑤ 自分の分だけ排水**。複数のスレッドがそれぞれ自分の `WSAPoll` で眠っていると、
  同じ 1 バイトが全員を起こすことがあります。先に目覚めた者がバッファを読み尽くし、
  後のスレッドは `recv` が空なのを見て、フラグをクリアして各自のループに戻ります。
  何度か余分に目覚めても無害です。キューこそ真実の源であり、起床は電子ベルであって、
  配達員ではありません。

`WSAPoll` がエラーを返した時の扱いにも心がけがあります：`WSAEINTR` は直接
返る（中断されたシステムコールは失敗ではない）、その他のエラーは
`std::system_error` を投げる——教育用ライブラリはエラーを飲み込みません。

## 対照表

| 問題 | asio（select の顔） | asio（epoll の顔） | sasio |
| --- | --- | --- | --- |
| 待機システムコール | `select()`（IOCP なしの Windows では WSAPoll） | `epoll_wait` | `WSAPoll` |
| descriptor集合 | ユーザー空間のスナップショット | カーネル側、登録は即座に有効 | ユーザー空間のスナップショット |
| 新しい登録で起こすか | 起こす（`start_unready_op`） | 起こさない（epoll は自然に見える） | 起こす（`if (first)`） |
| 新しい最早タイマーで起こすか | 起こす（`schedule_timer`） | timerfd なら不要 / さもなくば起こす（`update_timeout`） | 起こす（`add` が earliest を返す） |
| interrupter | socket pair / eventfd | eventfd / socket pair | 自己接続 UDP socket |
| ソース入口 | `detail/impl/select_reactor.ipp` | `detail/impl/epoll_reactor.ipp` | `src/reactor.cpp` |

一言で：**スナップショットモデルの複雑さは、すべて「目を覚まし続けること」に
費やされています**——epoll はdescriptor集合をカーネルに置き、省いたのは
まさにこの部分。select/WSAPoll ルートの起床ロジックの一段一段は、asio と
sasio で同型です。

## 練習

**実験**：1 回目の失守を手で再現します。`src/reactor.cpp` を開き、
`start_read` と `start_write` の 2 か所の `if (first) interrupt();` を
コメントアウトし、再ビルドして、`test_ip_tcp.exe` を単独実行します：

```text
cmake --build build --target test_ip_tcp
build\test_ip_tcp.exe
```

`tcp_multithread_wakeup_test` の結末を見てください：約 5 秒後に失敗——handler は
走らなかったのではなく、5 秒遅れただけです（ウォッチドッグタイマーが起こした）。
2 行を元に戻すと、テストは即座に通ります。あなたが検証したのは「スナップショット
モデルの代償」です：1 行の起床の欠如は、ミリ秒の配送を「次の偶然のイベントを待つ」
に退化させます。

**バグハント**：`src/ip_tcp.cpp` の `do_read()` は、`recv` が `would_block` を
返した時、エラーを報告する代わりに `reactor_.start_read(...)` を呼んで自分を
登録し直します。まず答えてください：poll が「読める」と言ったのに、`recv` が
どうして読めないことがあるのか？それから：もし登録し直さなかったら、この読み
操作はどんな状態になるでしょうか？（ヒント：コールバックはすでに表から移し出
されています——不変量 2。）

---

準備完了、起床、descriptorテーブルが揃いました。3 つの起床の源のうち、
残りは時間です。第 4 章のループは「次のタイマーの期限」で眠る時間を決めました——
その「次」はどこから来るのか。最小ヒープの維持、キャンセルが絶対に再入しない
理由、そして asio 自家のテストに捕まったヒープの範囲外アクセスのバグ。
次章：timer_queue。
