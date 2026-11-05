<!-- translated-from: book/src/ch13-tokens.md sha256:e20946bd683f -->

# completion token 层：結果の受け渡し方

executor 層は「どこで走るか」に答えました。この章——この層のもう半分——は
**結果をどんな形で呼び出し側に返すか**に答えます。同じ発起関数、

```cpp
socket.async_read_some(buffer, /* what goes here? */);
```

に対して、呼び出し側ごとに欲しいものがまったく違います：コールバックを欲しい人、
`std::future` を欲しい人、coroutineを欲しい人、結果に興味がない人
（発射したら忘れる）。asio の答えは、「ここに何を置くか」をコンパイル時の
パラメータにすること——**completion token** であり、特殊化の仕組みによって、
同じ発起関数が結果の形ごとに 1 回だけ実装されるようにします。

## 設計：token はコンパイル時のポリシー

機構を 3 つのルールに圧縮します：

1. **すべての発起関数は自分の完了シグネチャを宣言します**。`async_read_some` の
   シグネチャは `void(error_code, std::size_t)` です。発起関数は token 自体を
   処理しません——シグネチャと token をまとめて `async_result` に渡すだけです；
2. **`async_result<Token, Signature>` の特殊化が 2 つのことを決めます**：
   発起関数の戻り値の型、そして「エンジンに実際に渡される完了 handler の形」。
   デフォルトの特殊化（token が素のコールバックの場合）は `void` を返し、
   handler は token そのものです——あなたが 20 年間書いてきたコールバックコードは、
   すべてこのデフォルト特殊化を通っています；
3. **ユーザーは自分の token を定義できます**。新しい型のために `async_result` を
   特殊化すれば、それはこのライブラリの一級市民になります——`use_future`、
   `use_awaitable`、`deferred`、`detached` はすべてこの機構の**利用者**であり、
   エンジンのハードコーディングではありません。

sasio にはこの機構がありません。コールバックが唯一の形式で、coroutineは
`co_spawn.hpp` の明示的なアダプターに依存しています。本章は asio の特殊化体系を
開き、「sasio に取り付けるなら最小で何か」に答えます。

## trace：4 つの token、4 つの経路

同じ発起呼び出し、4 つの token、4 つの経路：

```text
callback:   async_read_some(buf, my_lambda)
              → the async_result default specialization
              → completion_handler = my_lambda
              → initiate → complete → my_lambda(ec, n) runs in run()

detached:   async_read_some(buf, asio::detached)
              → the detached specialization → the completion handler is
                a no-op object → complete → nothing happens
              (errors swallowed too — use with care)

use_future: async_read_some(buf, asio::use_future)
              → the use_future specialization → the return value becomes
                std::future<size_t>
              → the completion handler assigns the promise → you .get() elsewhere

use_awaitable: co_spawn(ctx, session(), asio::detached)
              → every co_await inside the coroutine resumes through the
                awaitable frame
              → the two ends of the same road as sasio's async_operation
```

第 2 章のトレースは「発起関数は何を返し、handler は誰か」を問いました——今は
正確に言えます：**その両方とも `async_result` の特殊化が決めます**。発起関数の
形は固定の骨格（`async_initiate`）に還元され、可変なのは token だけです。

## 精読：async_result の主テンプレート

`asio/async_result.hpp` の主テンプレート（ドキュメントコメントを除いたもの）：

```cpp
template <typename CompletionToken,
    ASIO_COMPLETION_SIGNATURE... Signatures>
class async_result :
  public conditional_t<
      detail::are_any_lvalue_completion_signatures<Signatures...>::value
        || !detail::are_any_rvalue_completion_signatures<Signatures...>::value,
      detail::completion_handler_async_result<CompletionToken, Signatures...>,
      async_result<CompletionToken,
        typename detail::simple_completion_signature<Signatures>::type...>
    >
{
public:
  typedef CompletionToken completion_handler_type;

  typedef void return_type;

  explicit async_result(completion_handler_type& h);

  return_type get();

  template <typename Initiation, typename RawCompletionToken, typename... Args>
  static return_type initiate(
      Initiation&& initiation,
      RawCompletionToken&& token,
      Args&&... args);
};
```

主テンプレートが記述するのは**デフォルトの挙動**（token がそのまま handler、
void を返す）です。token 型ごとに自分の特殊化を提供し、`return_type` と
`completion_handler_type` を上書きします。`initiate` が鍵の設計です——発起関数を
2 つに割ります：

- **token の処理**（特殊化の仕事）：use_future の特殊化は promise を作り、
  「promise に値を代入する」オブジェクトを完了 handler とし、`future` を
  戻り値として返します；
- **実際の発起**（`Initiation` の仕事）：処理済みの完了 handler を持って
  socket の `async_read_some` 実装を本当に呼び、エンジンに登録します。

割ることで、発起関数の作者は `Initiation` を 1 回だけ書けばよく、token の体系が
どう進化しても発起関数は変わりません。これが「1 つの実装、4 つの結果形式」の
仕掛けです。

## 3 列対照：欠落 ↔ 実装 ↔ 場所

| 能力 | sasio の現状 | asio の実装 | asio の場所 |
| --- | --- | --- | --- |
| コールバック（デフォルトの形式） | あり（唯一の形式） | `async_result` のデフォルト特殊化 | `asio/async_result.hpp` |
| future | なし | `use_future` token | `asio/use_future.hpp` |
| coroutineアクセス | あり（`co_spawn.hpp` の明示アダプター、token なし） | `use_awaitable` token + `co_spawn` | `asio/use_awaitable.hpp`、`asio/co_spawn.hpp` |
| 遅延起動 | なし | `deferred`（token がそれ自体 callable） | `asio/deferred.hpp` |
| 結果の破棄 | なし（handler が必須） | `detached`（no-op handler） | `asio/detached.hpp` |
| 発起関数の骨格 | 直接書く | `async_initiate` が token と発起を分離 | `asio/async_result.hpp` の `initiate` |
| 完成シグネチャの検査 | なし | `completion_signature` コンセプト | `asio/async_result.hpp`（`is_completion_signature`） |

規模の対比：sasio の `co_spawn.hpp`（113 行）は 1 つの形式——「coroutine
アクセス」を担います。asio の `use_awaitable.hpp` + `impl/co_spawn.hpp` +
`async_result` 体系は**任意の**形式に同じ発起実装を提供します。増えたのは
「より良いcoroutine」ではなく、「結果の形式そのものが拡張点になった」ことです。

## 練習

**実験**：sasio のために手書きの最小 future token を書きます。ジェネリックは
求めず、意味論だけ：`read_with_future(socket, buffer)` は
`std::future<std::size_t>` を返します（エラーは例外で）：

```cpp
std::future<std::size_t> read_with_future(
    sasio::ip::tcp::socket& socket, sasio::mutable_buffer buf)
{
  auto promise = std::make_shared<std::promise<std::size_t>>();
  auto future = promise->get_future();
  socket.async_read_some(buf,
      [promise](sasio::error_code ec, std::size_t n) mutable
      {
        if (ec)
          promise->set_exception(
              std::make_exception_ptr(std::system_error(ec)));
        else
          promise->set_value(n);
      });
  return future;
}
```

走らせた後、第 8 章と比べてください。この 10 行の関数がすることは
`write_op` の shared_ptr リレーと同じ種類です——「完了時にする動作」を状態と
ともにエンジンに渡す。さらに一歩：asio は結果の形式ごとに `read_with_xxx` を
書かないのはなぜですか？答えは `async_initiate` の分割の中にあります。

**バグハント**：第 11 章の sasio coroutineアダプター（`co_spawn.hpp` の自由
関数）は入口ごとに 1 つです。asio では同じ操作がコールバックと `use_awaitable`
の両方をどう支えるのか？`asio/use_awaitable.hpp` を開き、`async_result` 特殊化の
「token が完了 handler になる」部分を見つけ、答えてください：完了 handler が
呼ばれた時、coroutineを再開するのは誰か？（辿ると `awaitable_frame::resume` に
着きます——第 11 章 `awaiting_.resume()` の asio における同名の兄弟です。）

---

executor と completion token の 2 層がともに分解されました。本書の冒頭の問いに
答えられるようになりました：asio の 169,063 行のうち、何がモデルで、何が機械か？
次章は完全な読書マップを提示します——sasio の 9 章を asio のソースツリーに
対応させ、6 週間の読書計画を添えて。
