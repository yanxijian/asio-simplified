<!-- translated-from: book/src/ch03-services.md sha256:ac6cca903afe -->

# io_context がなぜここまで薄いか：service registry

第 2 章は未解決の問いを残しました：`async_read_some` の発起時には、バッファと handler を socket に保存し、reactor に関心を登録する必要があります。しかし socket はどこから reactor を手に入れるのか？reactor とtimer heap、handlerキューといった部品は、誰が作り、誰が持ち、いつ破壊するのか？

最も手軽な答えは、すべて `io_context` に押し込むことです：

```cpp
class io_context {
  Scheduler scheduler_;    // handler queue
  Reactor   reactor_;      // descriptor table
  TimerHeap timers_;       // timers
  // ...one more line here for every new feature
};
```

使えます。しかし asio の `io_context` を見てください——数百行のヘッダーの中に、具体的な機能に属する行はほとんどなく、「reactor メンバー」も見つかりません。手抜きではなく、この問題を別の層に委ねたのです：**service registry**。この層の存在こそ、`io_context` がなぜここまで薄いかの答えです。

## 設計：需要に応じて自己組み立てする容器

sasio の答えは 2 つの型にあります。`execution_context` が容器、`service` が部品の基底クラスです：

```cpp
class service {
public:
  virtual ~service() = default;
  virtual void shutdown() {}   // called at context destruction, reverse order
};

class execution_context {
public:
  template <typename T, typename... Args>
  T& use_service(Args&&... args);          // return if present, create if not

protected:
  void shutdown_services();                // reverse-order shutdown, then destroy
};
```

不変量は 2 つ、それぞれが 1 つのことを守ります：

1. **各サービス型は、context ごとに高々 1 つ**。最初の `use_service<reactor>()` が作成し、以後のすべての呼び出しは同じインスタンスを返します。誰が先に来て発起しても構いません——socket の構築時でも、scheduler の構築時でも、ユーザーコードでも；
2. **shutdown は作成の逆順で行われ、その後まとめて破棄されます**。後から作られたサービスが先に shutdown します。scheduler が構築時に reactor を作ったなら、片付けの時は reactor が先に shutdown し、scheduler が後に shutdown します——依存する側が先に去り、依存される側が後始末します。

`io_context` は `execution_context` を継承し、1 つのことだけをします：構築時にコンテナに scheduler を要求します。キュー、reactor、timer heapはすべてサービスで、需要に応じて現れます。新しい機能を追加する（第 10 章の strand、第 9 章のresolverなど）にも、`io_context` の 1 行も触る必要がありません。

## ある構築の再帰チェーン

「需要に応じた作成」が最も面白くなる瞬間は `io_context` の構築です：

```cpp
io_context::io_context()
  : scheduler_(use_service<scheduler>())   // first service in the container
{
}
```

この 1 行が引き起こすのは 1 回の作成ではありません。scheduler は reactor とtimer heapを管理するので、そのコンストラクタもコンテナにこの 2 つのサービスを要求します：

```cpp
scheduler::scheduler(execution_context& ctx)
  : reactor_(ctx.use_service<reactor>()),   // recursion: create the reactor
    timers_(ctx.use_service<timer_queue>()) // recursion: create the timer heap
{
}
```

つまり、最初の `io_context` の構築シーケンスは：scheduler がコンテナに登録 → scheduler 構築 → reactor 登録 → reactor 構築（Winsock 初期化、interrupter を開く）→ timer_queue 登録 → 構築完了 → scheduler に戻る → io_context に戻る。一本の再帰チェーンで、組み立て順序がそのまま依存順序になっています——グローバルな「初期化リスト」的な取り決めは一切不要で、必要な者が発起します。

このチェーンは不変量 2 がなぜ重要かも説明します。作成順序が scheduler → reactor → timer_queue なら、shutdown の逆順は timer_queue → reactor → scheduler：reactor が先に shutdown（interrupter と Winsock を閉じ）、scheduler が後に shutdown（キューを空にし、配送を停止）、最後にコンテナがまとめてオブジェクトを破棄します。順序が逆なら、scheduler の shutdown 内の `reactor_.interrupt()` は、すでに shutdown された interrupter に触れることになります。

## 精読：use_service の 20 行

この層の核心は 1 つの関数です。`include/sasio/execution_context.hpp` の実装を見てください（コメントは原文の英語のまま）：

```cpp
template <typename T, typename... Args>
T& use_service(Args&&... args)
{
  static_assert(std::is_base_of<service, T>::value,
      "a sasio service must derive from sasio::service");

  std::lock_guard<std::recursive_mutex> lock(mutex_);   // ①

  const std::type_index key(typeid(T));                 // ②
  const auto existing = services_.find(key);
  if (existing != services_.end())
    return static_cast<T&>(*existing->second);          // ③

  std::unique_ptr<T> owned(new T(*this, std::forward<Args>(args)...));
  T& created = *owned;                                  // ④
  services_.emplace(key, std::move(owned));
  order_.push_back(&created);
  return created;
}
```

4 箇所、それぞれに理由があります：

- **① 再帰ミューテックス**。普通の `mutex` は ④ でデッドロックします——`new T(*this, ...)` は T のコンストラクタを呼び、scheduler のコンストラクタは `use_service<reactor>()` を呼び、同じ関数に再入して同じロックを再び取るからです。`recursive_mutex` は同一スレッドの再入を許し、再帰的な組み立てチェーンが通るようにします。これは「需要に応じた組み立て」の直接の代償です；
- **② type_index を鍵に**。`typeid(T)` を `std::type_index` で包めばハッシュテーブルに入れられます——「scheduler というサービス」と「reactor というサービス」は自然に異なる鍵です。asio も typeid を使っていますが、自分で管理する登録構造で包んでいます；
- **③ ヒットすれば返す**。不変量 1 の「高々 1 つ」はここで果たされます——2 回目の要求は同じオブジェクトへの参照を受け取ります；
- **④ context がコンストラクタに渡る**。すべてのサービスの最初の構造引数は `*this` で、サービスは逆にコンテナを使えます（scheduler が reactor を手に入れるのはまさにこの方法）。作成後、`order_` に記録します——逆順 shutdown が歩く根拠です。

もう 1 つ立ち止まる価値のある箇所：`services_` は `unique_ptr<service>` の基底ポインタを保存し、`use_service` は返す前にダウンキャストします。ポリモーフィズムが「具体的にどんなサービスがあるか」の知識をすべて呼び出し側に押し出し、容器自身は無知を保ちます——これが `io_context` が薄い仕組みです。

## 対照表

| 問題 | asio | sasio |
| --- | --- | --- |
| 容器 | `execution_context.hpp` | `include/sasio/execution_context.hpp` |
| レジストリ | `detail/service_registry.hpp` | `use_service` テンプレートそのもの |
| サービス基底 | `execution_context::service` | `sasio::service` |
| 作成 | `make_service` + typeid 鍵 | `use_service` + typeid 鍵 |
| 再帰的構築 | 対応（service_registry が再入） | 対応（`recursive_mutex`） |
| 片付け | 逆順 shutdown | `order_` ベクターによる逆順 |
| asio 独自 | `add_service` / `has_service`、`notify_fork`、`shutdown` と `destroy` の分離 | なし（対応する需要がない） |

asio の service_registry は sasio にない設備をいくつか持っています：レジストリは `first_service_` リンクドリストでサービスをつなぎ、事後の `add_service` と照会 `has_service` を支え、`notify_fork`（プロセス fork 後の内部状態の再構築）を支え、「shutdown（資源の解放）」と「destroy（オブジェクトの破棄）」を 2 ステップに分けます。変わらないところ：サービス型ごと・context ごとに高々 1 つ——`basic_io_object.hpp` で I/O オブジェクトは構築時に `use_service` でこの共有サービスを受け取り、各オブジェクトは自分の `implementation_` だけを持ちます。「typeid 鍵 + 需要に応じた作成 + 逆順 shutdown」の骨格は完全に一致していて、asio の `service_registry` を読む時、ここの一行一行が見覚えのあるものであることに気づくでしょう。

## 練習

**実験**：レジストリを使って自分のサービスを書く——この context が実行した handler の数を数えます。全部で 30 行以内：

```cpp
class handler_stats : public sasio::service {
public:
  explicit handler_stats(sasio::execution_context& ctx) : service(ctx) {}
  void shutdown() override { std::printf("handlers: %zu\n", count); }

  void on_ran() { ++count; }

private:
  std::size_t count = 0;
};

// usage: wrap each post
auto& stats = ctx.use_service<handler_stats>();
ctx.post([&stats] { stats.on_ran(); /* your real work */ });
ctx.run();   // prints handlers: 1
```

走らせ、出力された数字が post の回数と一致することを確認してください。それから問い：2 つの `io_context` がそれぞれ `handler_stats` を掛けた時、カウントは互いに干渉しますか？（ヒント：不変量 1 の「context ごと」を読み直す。）

**バグハント**：`execution_context.hpp` の `recursive_mutex` を普通の `std::mutex` に変えて、再ビルドし、ch1 の `ex01_post` を実行してください。何が起きるか、どの行で起きるかを予測してから実行し、比較してください。デッドロックと予測したなら——ロックが stuck したのは何回目の進入か？スタックトレースの 2 層の `use_service` はそれぞれ誰が呼んだか？

---

サービス容器が整い、最初のサービス scheduler もすでに所定の位置にいます。しかし今のところ、「キューの中の handler は run() が実行する」と言ったきり——run ループの全文、work カウントの規則、「やることがない」の正確な判定は、まだ展開していません。次章は scheduler を分解します。ライブラリ全体で最も重要な 83 行です。
