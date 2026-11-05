<!-- translated-from: book/src/ch04-scheduler.md sha256:1feee4a767fd -->

# scheduler：handlerキューとワークカウント

第 3 章は scheduler を容器の中にブラックボックスとして据えました。この章で
その箱を開けます。まず、すでに使ったコードから——第 1 章の練習の例外ケースで、
`tests/asio/io_context.cpp` から引用します：

```cpp
ioc.post(&throw_exception);        // throws int
ioc.post(bind(increment, &count)); // counts normally
ioc.post(&throw_exception);

for (;;)
{
  try
  {
    ioc.run();
    break;                         // only a clean exit breaks out
  }
  catch (int)
  {
    ++exception_count;             // some handler threw an int
  }
}
// afterwards: count == 3, exception_count == 2
```

3 回の `run()`、2 回の例外、3 つのカウント handler はすべて実行済み。この用例の
裏には 4 つの問いが隠れています：handler が例外を投げた後、キューに残っているのは
まだ走るのか？`run()` は「まだやることがある、もう 1 周の価値がある」を何で
知るのか？例外を投げた「半分走った handler」が占めていた資源は誰が回収するのか？
そして最も根本的な——`run()` は一体何をループしているのか？

`scheduler` の 83 行の `run_one_impl` と、三十行あまりの記帳コード（post の
カウント、`handler_work`、四条件判定）が、4 つすべてに答えます。

## 設計：1 つのカウンターが 3 種の仕事を管理する

まずデータ構造。scheduler はキュー、カウンター、停止フラグを持ちます：

```cpp
std::mutex mutex_;
std::deque<std::function<void()>> queue_;   // handlers waiting to run
std::size_t work_count_ = 0;                // outstanding work units
std::atomic<bool> stopped_{false};
```

不変量は 3 つ。読み進める前に暗記してください：

1. **待ちごとに 1 つのワークユニットが対応し、終わったら即座に解放する——
   handler が例外を投げても解放する**。ここでの「待ち」はキュー内の handler だけを
   指します；タイマーとdescriptorはカウントを持ちませんが、やはり停止を
   阻止します（不変量 2）；
2. **`work_count_` がゼロ、キューが空、timer heapが空、登録descriptorなし
   ——4 条件がすべて空の時、scheduler は自分で stop します**。`io_context::work` を
   持っていれば、後の 3 つが空でも止まりません。stop 後もキューはそのまま保たれ、
   `restart()` してから続きを実行します；
3. **handler はキューの先頭から 1 つずつ取り出され、1 周に高々 1 つ**。実行前に
   スレッドマーカーを設定し（dispatch のインライン判定がこれに依存）、例外は
   呼び出しスタックを遡って `run()` から飛び出し、ここでは捕捉しません。

不変量 1 の「終わったら即座に解放」は、小さな RAII クラスが実現します。
`handler_work` は構築では何もしません（work 単位は `post` 時に計上済み）で、
破棄時に解放します：

```cpp
class handler_work
{
public:
  explicit handler_work(scheduler* s) : scheduler_(s) {}
  ~handler_work() { scheduler_->on_work_finished(); }
  // ...
};
```

このクラスが生きている期間＝work 単位が計上されている期間——これが asio の
`work_cleanup` の役割です。`handler_work` と `invoke` を組み合わせて見ると：
handler から例外が飛び出しても、スタック巻き戻しの中で `handler_work` の
デストラクタは確実に実行され、カウントは漏れません；例外はさらに上へ飛び、
`run()` から呼び出し者へ渡ります。これが asio の挙動：**例外はあなたのもの、
記帳はフレームワークのもの**です。

## 精読：run_one_impl 全文

さて 83 行のループを分解します。ライブラリで唯一ブロックする場所であり、
3 つのサービス（キュー、timer heap、reactor）が交わる場所です。部分ごとに。

**入口：run() の事前チェック。**

```cpp
std::size_t scheduler::run()
{
  if (!has_work())      // all four conditions empty: not worth entering
  {
    stop();
    return 0;
  }
  return run_impl(true);
}
```

空回りは即座に `stop()` して 0 を返します——これも asio の挙動：`run()` が
やることがないと判断すれば、context は stopped 状態に入り、その後に何かを
post しても、`restart()` するまで走りません。`poll()`、`run_one()`、
`poll_one()` の入口にも同じチェックがあります。

**ループ第 0 ステップ：まず停止フラグ。**

```cpp
for (;;)
{
  if (stopped())          // once stopped, no further handler is invoked
    return false;
```

停止の意味は「即座に切り上げ、キューはそのまま」：すでにデキューされた
handler は実行を終え（現在のこれ）、まだデキューしていないものは 1 つも
動かず、`restart()` を待ちます。テストの「例外が飛び出した後 `run()` を
やり直すと残りの handler が実行される」が検証しているのはまさにこれです。

**第 1 段：キューに handler があれば——1 つ取って、1 つ実行する。**

```cpp
std::function<void()> handler;
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!queue_.empty())
  {
    handler = std::move(queue_.front());
    queue_.pop_front();
  }
}

if (handler)
{
  handler_work work(this);
  invoke(std::move(handler));
  return true;
}
```

ロックの範囲に注目：ロックが守るのは「デキュー」の動作だけです。handler の
**実行はロックの外**で行われます——実行がロックを握っていたら、handler 内の
どんな `post`（同じロックを取る）も即座にデッドロックします。`run_one_impl` が
true を返すのは「この周で 1 つの仕事をした」ことを意味し、`run_impl` は
`while (run_one_impl(block)) ++count;` のカウントシェルです。

`handler_work` はここで構築されます——デキューの瞬間からこの handler の
work 単位は帳簿上に残り、実行が終わって（例外を含めて）デストラクタが
解放します。不変量 1、ここで果たされます。

**第 2 段：キューが空になった——タイマーを見る。**

```cpp
if (std::shared_ptr<timer_queue::entry> expired =
        timers_.take_expired(clock::now()))
{
  std::function<void(error_code)> complete = std::move(expired->handler);
  if (complete)
  {
    handler_scope scope(this);
    complete(error_code());
  }
  maybe_stop_if_idle();
  return true;
}
```

期限切れのタイマーは、run ループ内で handler を**直接呼びます**——第 2 章の
契約一を思い出してください：I/O の完了は post を通り、タイマーの完了はこの道を
通ります。`handler_scope` がスレッドマーカーを設定し（dispatch のインライン判定が
効くように）、`complete(error_code())`——空の error_code、「正常に期限切れ」。
呼び終えたら `maybe_stop_if_idle()`：これが最後の待ちだったなら、走り終えた
時点で止まるべきです。

**第 3 段：2 つの源が空——どれだけ眠れるか計算する。**

```cpp
bool work;
clock::duration timeout = clock::duration::zero();
{
  std::lock_guard<std::mutex> lock(mutex_);
  work = has_work_locked();
  if (work)
  {
    if (!block)                   // poll(): one non-blocking pass only
    {
      if (polled)
        return false;
      timeout = clock::duration::zero();
    }
    else if (timers_.empty())
    {
      timeout = clock::duration::max();   // no timers: sleep indefinitely
    }
    else
    {
      timeout = next timer expiry - now;  // sleep until then
    }
  }
}

if (!work)
{
  maybe_stop_if_idle();           // all four empty: record the stop
  return false;
}

reactor_.run_for(timeout);        // the only place that ever blocks
polled = true;
```

`has_work_locked()` は不変量 2 の実装で、4 条件を一度に見ます：

```cpp
return work_count_ != 0 || !queue_.empty() || !timers_.empty()
    || !reactor_.idle();
```

睡眠時間は 3 択：タイマーがなければ無限に眠る（`post`、新しいdescriptorの
登録、`stop` でしか起こされない——「どう起こされるか」は第 5 章で詳しく）；
タイマーがあれば最早のものが期限切れになるまで；`poll()` は非ブロッキングの
1 周だけ（`polled` フラグが `poll()` が reactor の上で回り続けるのを防ぎます）。

最後の `reactor_.run_for(timeout)` から目覚めたら、制御フローはループの頂上に
戻ります——停止フラグを見直し、キューを見直す。第 2 章の「毎周、3 つの源を
すべて再確認する」は、全文がこう落ちています。

## work の一生

不変量 1 と 2 に繰り返し現れる work 単位は、生から死まで 3 つの道しかありません：

- **キュー**：`post` で `++work_count_`、handler の実行が終わると
  `handler_work` のデストラクタで `--work_count_`；
- **ガード**：`io_context::work w(ctx);` は構築で `++work_count_`、破棄
  （または `reset()`）で `--work_count_`。「これからまだ仕事を入れる、止まるな」
  という意味——典型的な使い方は、マルチスレッドサーバでメインスレッドが 1 つの
  work を持ち、worker スレッドに context を止められないようにすること；
- **非同期解決**（第 9 章）：resolver のワーカースレッドが 1 つ持ち、
  `getaddrinfo` が戻るまで `run()` が抜けないことを保証します。

カウントがゼロになっても直接 stop するわけではありませんが、「四条件全空」を
次に成立させます——`on_work_finished()` はデクリメントの直後に
`maybe_stop_if_idle()` を呼ぶので、四条件が揃っていれば `stopped_` はその場で
立ち、run ループが次の周の先頭で観察します。asio の `work_finished()` も同じです。

## 対照表

| 問題 | asio | sasio |
| --- | --- | --- |
| ループ本体 | `detail/impl/scheduler.ipp` の `do_run_one()` | `src/scheduler.cpp` の `run_one_impl()` |
| work カウント | `outstanding_work_` | `work_count_` |
| 実行時の記帳 | operation の `work_cleanup` | `handler_work` RAII |
| 即時完了 | `post_immediate_completion()` | 直接 `post` |
| 停止条件 | `outstanding_work_ == 0`（構築時に計上しない選択あり） | 4 条件（タイマーとdescriptorを含む） |
| ガード | `executor_work_guard` | `io_context::work` |

意図的な差異を 1 つ点名：asio の `run()` は「キューだけでタイマーなし」の判定経路が
sasio と違います（asio のキュー内 handler も work として計上されるので 4 条件が
1 条件に潰れます；sasio のタイマーとdescriptorは無計上なので別々に見ます）。
**観察可能な挙動**は一致します：どんな種類の待ちでも、`run()` は抜けません。

## 練習

**実験**：work リークを作り、`run()` が引っかかるのを自分の目で見ます：

```cpp
sasio::io_context ctx;
sasio::io_context::work guard(ctx);   // forgot to reset
ctx.post([] { std::puts("done"); });
ctx.run();                            // never returns — why?
std::puts("never printed");
```

走らせて（それから Ctrl+C）。引っかかったループを本章の言葉で描写してください：
4 条件のうちどの 3 つが空で、どれが空でなく、`run_one_impl` の毎周はどの行で
眠るのか。対照実験：2 つ目の context は何も post せず、work を構築後すぐ
`reset()` すると、`run()` は 1 つの handler も実行せず **0** を返します：

```cpp
sasio::io_context ctx2;
sasio::io_context::work guard2(ctx2);
guard2.reset();                       // the unit goes back early
std::size_t n = ctx2.run();           // all four empty → stop, returns 0
std::puts(n == 0 ? "clean exit" : "unexpected");
```

2 つの context を並べると、「work は何の資格で run() を生かし続けるのか」と
「run() の戻り値は何か」の両方がはっきりします。

**バグハント**：第 1 章が残した 3 つの問いに、今度は自分で答えられます：

1. 最初の handler が投げた時、**それはすでにデキュー済み**で、例外とキューは
   無関係です。正常な handler はそのまま保たれ、catch 後の再実行で続きます——
   だから count == 3；
2. `stopped()` は **false**。例外は停止指示ではありません：停止は「四条件全空」
   か明示的な `stop()` だけで起きます。例外が飛んだ時キューにはまだ仕事があったので、
   停止フラグは一度も立ちませんでした——だから catch 後に `run()` を再呼びするのが
   合法です；
3. 3 回なのは、各 `run()` が最低 1 つの handler を処理してから例外で中断されるため；
   キューの 3 つの正常 handler は後続の周で実行され続け、3 つすべてが終わり、
   キューが空になり、四条件が揃い、`run()` が 0 を返して正常に終わった時、
   初めて `break` が条件を満たします。

予測と照らし合わせてください。3 つとも当たっていれば、あなたはすでに
実装者の立場でこのループを思考しています。

---

ループ、カウント、停止が揃いました。しかし第 3 段の `reactor_.run_for(timeout)`
はまだブラックボックスです——`WSAPoll` はどう呼ばれ、descriptorテーブルは
どう管理され、他のスレッドは眠っている人をどう起こすのか？次章は reactor を
分解します。そこには「2 回の起床漏れ」——この 1 行に埋められていた 2 つの
本物のバグがあります。
