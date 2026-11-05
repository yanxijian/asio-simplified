<!-- translated-from: book/src/ch12-executor.md sha256:3662da51bc7b -->

# executor 层：完了通知の配送ポリシー（delivery policy）

第 10 章はフックを残しました：sasio の strand は**明示的に渡した** handler しか
直列化しませんが、`async_write` の内部の中間完了は自動的についていきません——
asio ではこれを「自動的な継承」と呼び、executor 層の仕事です。本章は切り取られた
この層を講義します：何を解決するのか、機構はどんな形か、そしてもし sasio に
戻し入れるなら、最小で何をインストールするべきか。

前の 11 章と違い、本章の「精読」は本物の asio のシンボルを読みます——sasio に
対応物がないからです。それこそが Part V の読み方です：asio の実装に対して概念を
理解し、sasio の欠落を対照に使う。

## 問題：配送ポリシーは第一級の需要です

コールバック式の非同期には、これまで黙って通り過ぎてきた問いがあります：handler は
**どのスレッドで、どんなやり方で**実行されるのか？エンジンの 3 章の答えは常に
素朴でした——「`run()` のスレッドで、キューから取り出す」。シングルスレッドなら
それでいい。マルチスレッドの `run()` が始まった瞬間、この答えは足りなくなります。
3 つの本物のシナリオ：

1. **直列化**：ある session の読みと書きの handler は並行してはいけない——
   それが strand ですが、合成操作のすべての中間完了も strand に入るべきです。
   さもなければロックは迂回されます；
2. **オフロード**：解析し終えたデータの重い処理が I/O スレッドを占有すべきでは
   ない——ある handler を「別の場所で走らせ」、終わったら戻ってきてほしい；
3. **汎用化**：実行の場所を 1 つ追加するたび（スレッドプール、strand、カスタム
   スケジューラ）、socket 型はそれに応じてインスタンス化し直す——型の爆発です。

3 つをまとめると：**「どこで、どう走るか」は handler の第一級の属性であるべき**で、
エンジンに直書きされた規則ではありません。この属性こそが executor です。

## 設計：3 文のフレームワーク

asio の executor フレームワークは 3 文に圧縮できます：

1. **すべての handler は associated executor を持つ**。どこから来るのか？
   2 段階の検索チェーンです：handler 型が自分のを持っていれば
   （`bind_executor(strand, h)` が作る束縛オブジェクト）、それを使います；
   なければ発起側が渡したデフォルトを使います——I/O オブジェクトは発起時に
   自分の executor をデフォルトとして渡します。I/O オブジェクトの executor の
   デフォルトは `any_io_executor` で、構築時に受け取ったもの
   （通常は io_context の）を包んでいます；
2. **配送の動作は 3 種**：`post`（決してインラインではなく、常にキューへ）、
   `dispatch`（インラインの可能性あり——第 10 章の 2 つの判定条件がその
   実装です）、`defer`（post に似ていますが、完了チェーンが「直前に
   配送されたばかりの続き」である場合は割り込みをせず、エンジンに
   バッチ処理の機会を与えます）；
3. **strand は executor アダプターです**。`strand<Executor>` は内側の
   executor を包み、`post`/`dispatch` はやはり内側に向かい、キューイングと
   直列化が加わるだけ——だから strand の上のスレッドプール、strand の上の
   strand も組み合わせられます。

sasio は 1 の不完全版（`thread_local current_scheduler` が「フォールバック」の
役割をしています）、2 の半分（post/dispatch、defer なし）、3 のオブジェクト版
（executor ではなく context に結びついた strand）を持っています。欠けているのは
まさに「ポリシーをパラメータにする」その半分です。

## trace：1 回の束縛が伝わる様子

executor の価値は第 8 章の合成操作で最もはっきりします。asio のこの 1 行の
伝達チェーンを見てください：

```cpp
asio::async_read(socket, asio::buffer(buf),
    asio::bind_executor(strand, handler));
```

`bind_executor` が作るのは束縛オブジェクトです：handler と strand のパッケージ。
それから：

1. `async_read` の内部で `read_op` が組み立てられ、最終的な完了 handler は
   この束縛オブジェクトになります；
2. すべての層の**中間完了**（`read_op` が終わって次の短い区間を発始するとき）は、
   エンジンへの配送時に現在の handler の associated executor を尋ねます——
   束縛オブジェクトは strand を携えています；
3. したがってすべての中間完了も strand を通ってキューに入り、**チェーン全体が
   直列化されます**。

第 1 ステップで 1 回発起し、第 2 ステップで層ごとに効く——「1 回の束縛、
どこまでも有効」の機構は、**検索チェーンが「誰が呼んでいる」ではなく
「handler とともに」移動する**ことです。sasio の ex04 手動ラップは最外層の
lambda しか包まず、内側の完了（あれば）は漏れます。sasio の handler は裸の
`std::function` で、「私の executor はこれ」という属性を掛ける場所がないからです。

3 つの配送動作をシナリオに釘付けします：I/O の完了は post（インラインは
不可能——reactor コールバックのスタックの上にいて、再入は危険です）；ユーザーが
handler の中で呼ぶ `dispatch` はインラインが可能（すでに handler のスタックの上に
いるので、配送を 1 回節約できます）；完了チェーンの続きは defer（インラインは
しないが、「私は続きです」とエンジンに告げ、エンジンは列の後ろに並べる代わりに
すぐ続けて走らせることができる——キャッシュの局所性のための余地）。

## 精読：検索チェーンと型消去

この層を支える 2 つの asio シンボル。1 つ目は**検索チェーンの実装**、
`asio/associated_executor.hpp`：

```cpp
template <typename T, typename Executor = inline_executor>
struct associated_executor
{
  using type = typename detail::associated_executor_impl<T, Executor>::type;

  static type get(const T& t, const Executor& d = Executor())
  {
    return detail::associated_executor_impl<T, Executor>::get(t, d);
  }
};
```

`detail::associated_executor_impl` は 2 段階の判別です：T が自分の
`executor_type` を持っていればそれを使い（`executor_type` の特殊化）、T が
直接 `associated_executor` を特殊化していればそれを使い（`void_t` 特殊化）、
そうでなければ呼び出し側が渡したデフォルト `d` にフォールバックします（主
テンプレート）。注意：チェーンは 2 段階しかありません——「I/O オブジェクトの
executor」はチェーンの 1 段ではなく、**呼び出し側が渡すデフォルト引数**です：
socket は操作を発起する時に自分の executor を `d` として渡します。この
「trait + フォールバック」の形は asio の至る所にあります——
associated_allocator、associated_cancellation_slot も同型です。asio を読んで
「層を重ねた trait」を見たら、これが骨格です。

2 つ目は**型消去**、`asio/any_io_executor.hpp`：

```cpp
class any_io_executor :
  public execution::any_executor<
      execution::context_as_t<execution_context&>,
      execution::blocking_t::never_t,
      execution::prefer_only<execution::blocking_t::possibly_t>,
      execution::prefer_only<execution::outstanding_work_t::tracked_t>,
      execution::prefer_only<execution::outstanding_work_t::untracked_t>,
      execution::prefer_only<execution::relationship_t::fork_t>,
      execution::prefer_only<execution::relationship_t::continuation_t>
    >
```

これは**クラス**であり、`execution::any_executor<...>` を継承しています
（プロパティ名は `blocking_t::never_t` のようなネストした形）、7 つのプロパティが
「ブロック禁止」「work の記帳」「継続の関係」を照会・修飾可能な鍵に変えています。
これらのプロパティを満たす任意の executor を収められる消去コンテナです：内側は
小さいオブジェクト最適化のストレージ、呼び出しは仮想関数表を通ります。1.74 からは
すべての I/O オブジェクトのデフォルト executor 引数です——
`basic_stream_socket<tcp, any_io_executor>` の 1 つの型が io_context、
thread_pool、strand をすべてカバーします。代償は明示的です：配送ごとに 1 段の
間接呼び出し、小さいオブジェクトに入りきらない時は 1 回のヒープ割り当て。asio は
テンプレートの爆発をこの型消去で買い替えました。sasio は io_context が 1 つ
だけで、この層すら必要ありません。

## 3 列対照：欠落 ↔ 実装 ↔ 場所

| 能力 | sasio の現状 | asio の実装 | asio の場所 |
| --- | --- | --- | --- |
| 指定した実行体への完了配送 | なし（handler は裸の `std::function`、配送は scheduler 固定） | associated executor 検索チェーン + `bind_executor` | `asio/associated_executor.hpp`、`asio/bind_executor.hpp` |
| strand の直列化 | あり（context に結びついたオブジェクト） | executor アダプター、組み合わせ可能 | `asio/strand.hpp`、`detail/impl/strand_service.ipp` |
| スレッドプールへのオフロード | なし | `thread_pool`（それ自体が executor） | `asio/thread_pool.hpp` |
| 実行体の実行時選択 | なし（io_context は 1 つ） | `any_io_executor` の型消去 | `asio/any_io_executor.hpp` |
| 実行プロパティ（ブロック禁止、続作のバッチ等） | なし | `execution::` プロパティフレームワーク | `asio/execution/*.hpp` |
| 継続の配送（defer） | なし | `defer` + continuation の意味論 | `asio/defer.hpp` |
| インライン配送 | あり（2 つの判定条件、ch10） | `can_dispatch()` + `dispatch` | `detail/impl/scheduler.ipp` |

「sasio の現状」が「あり」の行の実装は、前の章ですべて分解しました——エンジンの
2 行（strand、dispatch）はまさに executor フレームワークの中で**パラメータ化なしでも
成立する**部分です。これが逆方向から sasio の判断を説明します：エンジンの
幹（ch3–11）は executor なしでも自己完結し、それを必要とするのはすべて
「汎用化」の需要です。

## 練習

**実験**：ex04 の手動ラップで「自動継承なし」を証明します。ex04 の上に
2 段跳びのシナリオを組み立てます：

```cpp
auto bound = bind_strand(strand_, [&](error_code) {
  std::puts("first hop: strand=1");
  const auto hop1_thread = std::this_thread::get_id();

  timer_.async_wait([&](error_code) {      // hop two: unwrapped
    const bool in_strand = strand_.running_in_this_thread();
    const bool same_thread = std::this_thread::get_id() == hop1_thread;
    std::printf("second hop: strand=%d, same_thread=%d\n",
        in_strand ? 1 : 0, same_thread ? 1 : 0);
  });
});
timer_.async_wait(bound);
```

予測：hop 1 の `running_in_this_thread()` はいくつ？hop 2 は？走らせて比較
——hop 2 は strand から外れました。あなたの手動ラップが包んだのは hop 1 の
完了だけだからです。これが「bind_executor は 1 回の束縛で至る所有効」の
裏面教材であり、asio が検索チェーンを handler 体系に組み込んだ理由です。

**バグハント**：`asio/strand.hpp` を開き、このアダプターのインターフェイス面を
数えてください：4 つの配送メンバー（`post` / `dispatch` / `defer` /
`running_in_this_thread`）のほかに、`query` / `require` / `prefer` があります
——executor プロパティプロトコルの三つ組です。`sasio::strand` が欠けている
部分をこれらのメンバーに対して列挙し、答えてください：`query` がないせいで、
どんな asio コードが移植できなくなりますか？（ヒント：strand にその下の
context を尋ねる `asio::query(s, execution::context)` のようなコード。）

---

executor は「どこで走るか」に答えました。この層のもう半分——「結果をどう
返すか」——が残っています。completion token とその `async_result` 体系は
次章で分解します。
