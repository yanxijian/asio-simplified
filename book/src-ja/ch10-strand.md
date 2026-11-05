<!-- translated-from: book/src/ch10-strand.md sha256:c98c21cf9bfd -->

# strand：1 つのキューと 1 つの running フラグ

第 2 章で述べました：マルチスレッドが同時に `run()` を呼ぶと、異なる handler が
並行に実行されます。第 4、5 章はこの並行性の部品をすべて提供しました——2 つの
スレッドがそれぞれ `WSAPoll` で眠り、それぞれキューから取り出し、それぞれ実行する。
ここで代償が顕在化します：echo サーバの中で、同じ session の `do_read` と
`do_write_some` が **同時に** 2 つのスレッドの上で走る場面は、遅かれ早かれ必ず来ます。
`write_offset_` のような普通のメンバーは即座にデータ競争の的になります。

asio の答えは strand と呼ばれます：**一連の handler に「決して並行しない」という
印を付ける**。sasio の `strand` は `io_context` に結びついた小さなオブジェクトで、
`include/sasio/strand.hpp` と `src/strand.cpp` の合計は 272 行——この章で開き、
ついでに、つい最近起きたバグも講義します：このリポジトリのレビュープロセスが
執筆期間中に捕まえた strand 意味論の偏差が、第 5 章の 2 回の失守と並んで
コミット履歴に横たわっています。

## 設計：キュー、フラグ、ドレインループ

strand の内部状態は 3 つだけです：

```cpp
scheduler& scheduler;                     // the drain borrows the scheduler
std::mutex mutex;
std::deque<std::function<void()>> queue;  // queued handlers
bool running = false;                     // is a drain loop running
```

不変量は 2 つ：

1. **同一 strand の handler は並行せず、`post` の順序が実行順序です**。実装の
   方法は handler 自体をロックすることではなく、**いつでも高々 1 つのドレイン
   ループ（drain）だけが走っている**ことです：`running` フラグがその「論理ロック」
   です；
2. **handler が例外を投げても strand は死なない**。ドレインループは例外経路で
   `running` を解放し、残りの handler を再スケジュールし、それから例外を
   上へ投げ続けます——strand の直列化保証は例外を呑まず、例外によっても
   働かなくなります。

`running` は `std::mutex::lock` のようなブロック意味論ではありません：strand を
取れなかった handler は待たず、**キューに入ります**——これこそ「非並行」と
「呼び出し者をブロックしない」の両立点です。キューへの追加そのものは strand 内部の
mutex（細粒度、臨界区間は極短）が守り、本物の直列化は `running` が保証します。

## あるドレインの一生

`s.post(h)` から、h の一生は 4 ステップです：

1. **入隊**：h が `queue` に入ります。ロック内で `running` を検査：ドレインループが
   走っていなければ、true にして「ドレインをスケジュールする必要あり」と記録します；
2. **スケジュール**：scheduler に匿名関数を `post` します——その関数は 1 つの
   ことだけをします：`drain()` を呼ぶ。この匿名 handler は普通の handler と同じく
   キューに入り、実行され、work 単位を占有します。scheduler にとって strand は
   何の特殊性もありません；
3. **ドレイン**：`drain()` はループです——ロック内で `queue` から 1 つ取り出し、
   ロック外で実行し、空になったら `running` を false にして返ります。実行中に
   新しく post された handler はキューに入るだけで、**同じ**ループが次の周で
   続けて取り出します；
4. **収尾**：キューが空、`running` を false に。次の post は「空いている」
   strand を見て、第 1 ステップからやり直します。

この形の妙味は第 3 ステップにあります：**ドレインループが「実行権」と「キュー」を
結びつけます**。`running` が true である限り、世界中の `s.post` はキューへの
追加にすぎません——実行権はずっと drain の手にあり、順序は自然に post の順序
です（不変量 1 の後半）。

`drain` の例外処理は独立して見る価値があります：

```cpp
try
{
  execute(std::move(handler));
}
catch (...)
{
  reschedule_after_exception();
  throw;                          // the exception keeps flying out of run()
}

void strand::impl::reschedule_after_exception()
{
  bool more = false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
    if (!queue.empty())
    {
      running = true;
      more = true;
    }
  }
  if (more)
    schedule_drain();
}
```

例外が飛び出した時、`running` はまだ立っています——処理しなければ、strand は
永遠にロックされます。`reschedule_after_exception` はまず解放し、残りの handler を
再ドレイン（新しい 1 回の `post`）し、最後に `throw` で例外を asio の規則どおり
`run()` から飛ばします。テスト `strand_test` の例外セクションが検証しているのは
まさにこれ：例外を投げる handler 2 つが正常な handler 3 つを挟み、3 回の `run()` の
後で count == 3、exception_count == 2 です。

## dispatch の 2 つの経路：1 つの共通の判定条件

strand には `post` と `dispatch` の 2 つの入口があります。`post` は単純です
（常にキューに入れる）；`dispatch` の意味論は「できればその場で」——**しかし何を
「できる」と呼ぶのかは、strand と scheduler で一致していなければなりません**。
まずコード、`src/strand.cpp` の `impl::dispatch` から：

```cpp
if (current_strand_impl == this)        // already inside this strand: inline
{
  execute(std::move(handler));
  return;
}

bool run_now = false;
bool schedule = false;
{
  std::lock_guard<std::mutex> lock(mutex);
  // asio: can_dispatch() && !locked_
  if (scheduler.can_dispatch() && !running)
  {
    running = true;
    run_now = true;                     // inline execution
  }
  else
  {
    queue.push_back(std::move(handler));
    if (!running)
    {
      running = true;
      schedule = true;                  // idle strand + outside run: queue + drain
    }
  }
}
// run_now → execute + drain; schedule → schedule_drain()
```

3 つの分岐、判定は 1 文だけ：**呼び出しスレッドが `run()` の中にあり、対象が
現在空いている**。scheduler の `dispatch` は context を見
（`current_scheduler == this`）、strand は strand 自身を見ます
（`current_strand_impl == this`）——対象は違いますが、原則は同じです。asio の
2 つの実装はそれぞれ `can_dispatch()`（scheduler.ipp）と
`can_dispatch() && !locked_`（strand_service.ipp）と書かれており、sasio は
両方と同型です。

この「共通の判定」は生まれつきのものではなく、直されたものです。初期版の
sasio `strand::dispatch` は `running` だけを見ていました：strand が空いて
いればその場で実行する——**呼び出しスレッドが `run()` の中になくても**。そこで
`strand s(ctx); s.dispatch(h);` は `run()` が始まる前に h を実行し、第 2 章の
契約二に直接違反しました。このリポジトリのレビュープロセスが捕まえ、修復は
asio `strand_service::do_dispatch` の `can_dispatch() && !locked_` に揃え、
回帰テストを補いました：

```cpp
void strand_dispatch_outside_run_test()
{
  io_context ioc;
  strand s(ioc);

  bool ran = false;
  s.dispatch([&ran] { ran = true; });

  ASIO_CHECK(!ran);      // fails before the fix: the old code ran it inline
  ioc.run();
  ASIO_CHECK(ran);       // the handler executed inside run()
}
```

インライン実行の 3 つの経路を並べて、第 2 章の契約一の総勘定とします：

| 経路 | インライン条件 | 判定対象 |
| --- | --- | --- |
| `scheduler::dispatch` | `current_scheduler == this` | context |
| `strand::dispatch` | `can_dispatch() && strand が空き` | strand |
| `strand::dispatch`（すでに strand 内） | `current_strand_impl == this` | strand |
| タイマーの期限切れ（`run_one_impl`） | run ループが直接呼ぶ、キューを経由しない | —— |

入れ子の意味論もこの表の中にあります：すでに strand を持っているスレッドが
さらに `dispatch` すれば、必ずその場で実行されます（asio も同様——テスト
`increment_with_lock` がアサートしているのはまさにこれ）。

## 精読：thread_local の 2 回のスタック出入り

`execute` と `running_in_this_thread` は 1 つの thread_local 変数を共有します：

```cpp
void execute(std::function<void()> handler)
{
  void* previous = current_strand_impl;
  current_strand_impl = this;          // push

  struct restore
  {
    void* previous;
    ~restore() { current_strand_impl = previous; }   // pop
  } guard = {previous};

  handler();
}
```

保存しているのは「このスレッドがどの strand の上にいるか」ではなく**スタック**です
——`previous` が外層を保存し、デストラクタが復元します。なぜスタックが必須か？
handler の中で、**別の** strand の handler を `dispatch` することができるからです：
内側の strand の `execute` は変数を自分に変え、終わる時に外側に復元します。
入れ子の strand の `running_in_this_thread` は、それによってどの層でも正しく
答えます。

asio との対比：asio はポインタを保存せず、strand の impl を handler の
associated executor に付け、`running_in_this_thread` は executor の呼び出しスタック
比較によります。thread_local スタックは、executor のない前提での sasio の最小
等価物で、意味論は一致します：**「自分はどの strand の上にいるか」は実行時の
動的な事実であって、コンパイル時の帰属ではない**。

## 対照表

| 問題 | asio | sasio |
| --- | --- | --- |
| 形態 | `strand<Executor>` executor アダプター + `detail/strand_service` | io_context に結びついたオブジェクト + `impl` |
| ドレイン | `strand_impl::run()`（operation の完了が駆動） | `drain()` ループ |
| インライン判定 | `can_dispatch() && !locked_` | `can_dispatch() && !running` |
| 入れ子の認識 | executor の呼び出しスタック | `thread_local` + 保存/復元 |
| 結合の構文 | `bind_executor(strand, handler)` | `s.post(...)` / 手動ラップ（下記） |
| ソース入口 | `strand.hpp`、`detail/impl/strand_service.ipp` | `src/strand.cpp` |

「自動的に継承できない」は sasio の差異表で最も価値のある 1 条です：asio の
`async_read(socket, buf, bind_executor(strand, h))` は**合成操作の内部のすべての
中間完了**を strand に通らせます。sasio の手動ラップは、明示的に包んだその 1 段だけしか包みません。`examples/ex04_bind_executor.cpp` はラップの姿を
10 行で示し、それができないことも示しています——この缺口の原理
（associated executor の検索チェーン）は第 12 章の主題です。

## 練習

**実験**：`tests/asio/strand.cpp` のマルチスレッド用例を走らせ、それから
破壊実験をします——`strand_multithread_test` の `s.post(...)` をすべて
`ioc.post(...)` に変えて（strand をバイパスして）、もう一度走らせます。
`max_active` のアサーション結果を見てください：strand を使わないと、2 つの
`run()` スレッドが並行実行する handler のピークは 1 ではなくなります。この対比が
strand の全価値であり、「どんな状態に strand の保護が必要か」の判別実験でも
あります。

**バグハント**：本章の冒頭に述べた strand dispatch バグは、リポジトリの履歴の
中に横たわっています。`tests/asio/strand.cpp` で
`strand_dispatch_outside_run_test` を見つけ、名前を変更して実行されないようにし、
`src/strand.cpp` の dispatch のインライン条件を
`scheduler.can_dispatch() && !running` から一時的に `!running` に戻して
（初版の偏差を復元）、再ビルドして実行します。2 つのアサーションのどちらが先に
失敗しますか？失敗の時、handler はどのスレッドで実行されましたか
（デバッガで呼び出しスタックを一目見てください——スタックの底は
`strand_dispatch_outside_run_test` であって `run` ではありません）？正しい
実装に戻します。あなたはこのリポジトリのレビュープロセスの最初の虫捕りを
自分の手で再現したことになります。

---

直列化が補えたので、コールバック式の asio は完成です。最後の現代的な
ピースが残っています：コールバックを直線のコードとして書くこと——`co_await`。
次章は `awaitable<T>` と `co_spawn` を分解します：promise、symmetric
transfer、そして「完了 handler がデストラクタの中に隠れている」締めの設計。
