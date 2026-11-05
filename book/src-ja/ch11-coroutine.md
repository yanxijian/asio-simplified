<!-- translated-from: book/src/ch11-coroutine.md sha256:67c19c8bed8d -->

# coroutine：コールバックを直線コードとして書く

第 8 章の `write_op` は shared_ptr のリレーで 1 段のコールバックを鎖にしましたが、
本物のプロトコルコードはもっと多いです——「メッセージヘッダーを読む、次にボディを
読む、解析して、書き戻す、ループする」という 4〜5 段の非同期ステップが続くと、
コールバック式ではこう育ちます：

```cpp
void do_read_header(std::shared_ptr<session> self)
{
  async_read(self->socket_, header_buf,
      [self](error_code ec, std::size_t) {
        if (ec) { close(self); return; }
        self->parse_header();
        async_read(self->socket_, body_buf,
            [self](error_code ec, std::size_t) {
              if (ec) { close(self); return; }
              // ...each step deeper, each piece of state further away
            });
      });
}
```

制御フローがネストした lambda に細断されます：共有状態は `self` のキャプチャ頼み、
エラー処理は層ごとのコピー、早期リターンの経路は手動で追跡。C++20 coroutineは
同じロジックを直線に書かせてくれます：

```cpp
sasio::awaitable<void> session(tcp::socket& socket)
{
  char header[16];
  co_await sasio::async_read_some(socket, sasio::buffer(header));
  // parse_header() sits right here
  char body[64];
  co_await sasio::async_read_some(socket, sasio::buffer(body));
  // ...
}
```

本章は `include/sasio/awaitable.hpp` と `co_spawn.hpp` を開き、3 つの問いに答えます：
`co_await` がサスペンドした時、何が起きたのか？coroutineが終わる時、完了通知は
どこから来るのか？そして、なぜ sasio のcoroutineに asio の completion token の
仕組みが不要なのか？

## 設計：タスク型とその約束

`awaitable<T>` は**coroutineの戻り値型**です——関数がこれを返すと宣言すれば、
体内で `co_await` / `co_return` が使えます。コンパイラは関数の本体を大きな
ステートマシンに書き換え、その記帳規則を `promise_type` が提供します。sasio の
promise には 5 つのメンバーがあり、それぞれがcoroutineの生涯のある問いに答えます：

```cpp
struct promise_type
{
  std::optional<T> value;        // co_return lands here
  std::exception_ptr error;      // unhandled_exception lands here
  std::function<void(std::exception_ptr, T)> completion;  // top level: who gets the result
  std::coroutine_handle<> continuation;                   // nested: who gets control back
  bool top_level = false;        // set by co_spawn; decides the ending
};
```

不変量は 3 つ：

1. **awaitable はcoroutineフレームを排他的に所有します**。move-only
   （コピーは削除済み）で、`co_await` は右値しか受け付けません——名前付きの
   変数に対する `co_await task` はコンパイルできず、`co_await std::move(task)` と
   書きます。見せびらかしではありません：フレームの破棄権を所有する者は
   正確に 1 人でなければならず、2 つのコード経路がどちらも自分が保持していると
   思えば、それは double free です；
2. **トップレベルとネストの終わり方は違います**。`co_spawn` が起動した
   トップレベルのcoroutineは `final_suspend` で**サスペンドしません**——フレームは
   その場で破棄され、完了 handler は promise のデストラクタの中で呼ばれます。
   `co_await` されたネストのcoroutineは `final_suspend` でサスペンドし、
   symmetric transfer で制御権を待ち手に返し、フレームは待ち手が解放します；
3. **非同期の完了は run ループに戻り、同期の完了は発起スタックに戻ります**。
   誰がcoroutineを resume したか、そのスレッドの上に完了通知があります：
   サスペンドしたことがあるcoroutineは engine の完了配送で再開されます
   （`run()` の中）；一度もサスペンドせず直接 `co_return` で終わったcoroutineは、
   `co_spawn` の最初の `resume()` の中でフレームが破棄されます——完了 handler は
   **co_spawn の呼び出しスタック**の上で実行され、`run()` の外かもしれません。
   自分の handler がどのスタックの上で走るのか、書く時に意識してください。

## あるcoroutine的一生

テストの実例をトレースします：

```cpp
sasio::awaitable<int> return_after_wait(sasio::steady_timer& timer)
{
  co_await sasio::async_wait(timer);
  co_return 42;
}

// in the test:
co_spawn(ioc, return_after_wait(timer),
    [&](std::exception_ptr e, int result) { value = result; });
ioc.run();
```

`co_spawn` から `42` が着地するまで、8 ステップ：

```text
① co_spawn: take the handle, set top_level=true, store completion, resume()
② the body runs → co_await async_wait(timer)
③ async_wait builds async_operation<void>, co_awaits it
    → await_suspend: remember awaiting_ (this coroutine's handle), call start(...)
    → start calls timer.async_wait(handler), registering in the heap
    → async_operation::await_suspend returns → the coroutine suspends,
      control returns to co_spawn
④ co_spawn returns (the coroutine is asleep on the timer)
⑤ the timer expires → the run loop invokes the timer's completion handler
    → the handler calls awaiting_.resume(): the coroutine continues at co_await
⑥ async_operation::await_resume: error_ empty → normal return
⑦ co_return 42 → promise::return_value(42) → final_suspend()
    → final_awaiter::await_ready() == true (top_level) → no suspend
    → the frame destroys on the spot → ~promise_type calls completion(no error, 42)
⑧ the test's lambda receives 42
```

第 ③ ステップで立ち止まる価値があります：**coroutineのサスペンド点は、
「1 つの非同期操作を登録すること」です**。`async_operation` がコールバック式の
発起関数（`timer.async_wait(...)`）を co_await 可能なオブジェクトに包みます——
発起、登録、サスペンドが一息で行われます。コールバック式とcoroutine式を
つなぐ橋はこの 1 つだけで、橋の両岸は同じエンジンです。

## 精読：final_awaiter とデストラクタに隠れた完了

coroutineの締めは実装で最もねじれた部分ですが、sasio は 20 行に圧縮しました：

```cpp
struct final_awaiter
{
  promise_type* self;

  // Top-level coroutines (started by co_spawn) do not suspend at the end:
  // the frame is destroyed automatically and ~promise_type runs the
  // completion handler. Awaited coroutines do suspend and hand control to
  // their awaiting coroutine via symmetric transfer.
  bool await_ready() const noexcept { return self->top_level; }

  std::coroutine_handle<> await_suspend(
      std::coroutine_handle<promise_type> handle) noexcept
  {
    std::coroutine_handle<> next = handle.promise().continuation;
    return next ? next : std::noop_coroutine();
  }

  void await_resume() noexcept {}
};
```

`co_return` の後、コンパイラは `final_suspend()` を呼び、この awaiter を
受け取ります：

- **トップレベル**（`top_level == true`）：`await_ready` が true を返し、
  サスペンドをスキップします——本体が終わり、フレームは**その場で破棄**されます。
  `~promise_type` はフレーム破棄の最後の立ち寄り点で、完了通知はそこから
  出て行きます：

  ```cpp
  ~promise_type()
  {
    if (completion)
    {
      try
      {
        completion(error, value ? std::move(*value) : T{});
      }
      catch (...) {}
    }
  }
  ```

  例外と戻り値が 1 回の呼び出しに合流します。completion の中で何を投げても
  呑み込まれます——「結果の引き渡し」はcoroutineの最後の動作であり、その後の
  throw には受け手がいません。完了 handler は**インラインで**呼ばれることに注意：
  それはcoroutineを最後に resume したスレッドです——そしてその resume は
  engine の完了配送から来た（ステップ ⑤）ので、契約二は守られています。

- **ネスト**：`await_ready` が false を返し、`await_suspend` に入ります——
  戻り値は**次に再開するcoroutineのハンドル**（待ち手）で、待ち手がいなければ
  `std::noop_coroutine()` を返します。コンパイラはそのハンドルへ直接ジャンプして
  再開し、**呼び出しスタックの層を追加しません**——これが symmetric transfer です。
  これがなければ、深くネストしたcoroutineのチェーンは戻るたびにスタックを
  1 段ずつ重ね、長いチェーンでスタックがあふれます。あれば、制御権はリレーの
  バトンのように移動するだけです。

なぜトップレベルは「その場で破棄」して、誰かに回収されるためにサスペンドしない
のか？トップレベルには待ち手がいないからです——completion 以外に、その存在を
知る者はいません。誰も再開しに来ないcoroutineをサスペンドさせておくことは、
フレームを 1 つリークすることです。「サスペンドしない → 破棄 → デストラクタが
結果を渡す」で、リークの穴を完全に溶接しました。

## 対照表

| 問題 | asio | sasio |
| --- | --- | --- |
| タスク型 | `awaitable<T, Executor>`（executor 付き） | `awaitable<T>`（executor なし） |
| フレームの記帳 | `impl/awaitable.hpp` の `awaitable_frame_base`（`awaitable_launch_context` を継承、カスタム `operator new` がフレームをリサイクル） | `promise_type` + ハンドルの所有権 |
| 起動 | `co_spawn`（executor 経由で配送） | `co_spawn`（直接 `resume()`） |
| 完了通知 | `async_result` の特殊化 + token（`use_awaitable`） | `promise_type::completion` |
| サスペンド/再開 | `awaitable_frame::resume()` | `awaiting_.resume()` |
| ソース入口 | `awaitable.hpp`、`impl/awaitable.hpp`、`impl/co_spawn.hpp` | `include/sasio/awaitable.hpp`、`co_spawn.hpp` |

規模の差の根は第 4 行にあります。asio の `co_spawn(ctx, task, token)` の token は
`use_awaitable`、`use_future`、detached... を受け付けます。「結果をどう返すか」は
token が決めるので、完了通知は `async_result` の特殊化体系を通らなければなりません
（第 13 章の主題）。sasio は token を切り、完了通知を promise のデストラクタに
直接書きました——一文で済む話です。代償も台帳にあります：`use_future` が
使えない、detach ができない、入口ごとに自由関数のアダプターが 1 つ
（`co_spawn.hpp` の `async_wait` / `async_read_some` など）。

もう 1 つの規模差の源：asio の `awaitable_frame_base` は
`awaitable_launch_context` を継承し、`operator new` をカスタマイズして
**フレームメモリのリサイクル**をしています。co_spawn の work カウントは
`co_spawn_state` の work guard（`impl/co_spawn.hpp`）に掛かっていて、フレーム内には
ありません。sasio のcoroutineフレームは「素」のものです：メモリはコンパイラの
coroutine機構に任せ、work は**下層の操作**（タイマーや socket の登録）に
依存します。効果は同じ（coroutineが待っている間、`run()` は抜けません）が、
機構は違います——asio はフレームをリソースプールとして管理し、sasio は
フレームを普通のオブジェクトとして扱います。

## 練習

**実験**：**再帰的な**coroutine鎖を書いて、symmetric transfer がスタックを
食わないことを検証します。注意：「順番に千回 await する」はネストではありません
——どの瞬間でも待っているのは 1 段だけです。本当の深い鎖を作るには、
coroutineがcoroutineを await する必要があります：

```cpp
sasio::awaitable<int> chain(sasio::steady_timer& t, int depth)
{
  if (depth == 0)
  {
    co_await sasio::async_wait(t);            // the only suspension, at the bottom
    co_return 0;
  }
  co_return co_await chain(t, depth - 1);     // recursion: 1000 frames alive at once
}

// co_spawn(ioc, chain(timer, 1000), [&](std::exception_ptr e, int r) { ... });
```

`chain(1000)` は 1000 段のcoroutineフレームを同時に保持します
（各層が次の層を待つ）；1000 のサスペンド点のうち**最も深い 1 つだけがタイマー**で、
残りの 999 は「子coroutine」を待っています。タイマーが発火した後、完了は 1000 层の
`co_return` を辿って 1 层ずつ上に届けられます——一跳びごとに symmetric transfer で、
デバッガで呼び出しスタックを見ると、深さは層数とともに増えません
（普通の再帰関数と比べてください：1000 回の呼び出しは 1000 個のスタックフレーム）。
通った後、`chain` の `final_suspend` を `std::suspend_always{}` に変えて
（サスペンドしても誰も再開しない）もう一度走らせます——観察される現象：鎖は
どこかの層で凍結し、`run()` はタイマーの鎖が切れたため早期に停止します。
不変量 2 の反面教材そのもの：**誰にも回収されないフレームはリークです**。asio の
awaitable_frame はフレームリサイクルプールでこれを支え、sasio は「トップレベルは
必ず破棄、ネストは必ず await される」という約束で支えます。

**バグハント**：次のコードは `co_await` の行でコンパイルが失敗します：

```cpp
auto task = return_after_wait(timer);
co_await task;        // ← compile error
```

2 つの問い：エラーメッセージのどの言葉が原因を示していますか？正しい書き方は
何ですか？さらに一歩——なぜコピーを許すより move-only の方が安全なのか？
ヒント：1 つの awaitable の 2 つのコピーがどちらも co_spawn されたら、誰が
coroutineフレームを破棄する責任を持ちますか？

---

コールバック式とcoroutine式が両方揃いました。しかし第 12 章まで、本書は
ずっと「切り取られた合成層」に触れてきました：executor と completion token。
16.9 万行のうちの数万行に、それらはなぜ値するのか？次章は executor を
開きます。
