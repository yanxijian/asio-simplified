<!-- translated-from: book/src/ch01-why.md sha256:36bdae6a16cb -->

# なぜまず 4,200 行の版を読むのか

[chriskohlhoff/asio](https://github.com/chriskohlhoff/asio) のソースを開いてみてください。バージョン 1.38.2 の `include/asio` ディレクトリには 600 個の `.hpp` ヘッダー、169,063 行があります（`.ipp` 実装ファイルまで数えれば約 19.7 万行）。その前に、あなたはおそらく API を使ったことがあるだけでしょう：

```cpp
asio::io_context io_context;
asio::ip::tcp::socket socket(io_context);
socket.async_read_some(buffer, handler);
io_context.run();
```

4 行で、直感的には難しくありません。しかし「`run()` は一体何を走らせているのか」「handler は誰で、どのスレッドで、いつ呼ばれるのか」に答えようとして `async_read_some` を辿ると、最初の画面に現れるのは `basic_stream_socket` のテンプレート宣言——プロトコルと executor の 2 段階のパラメータ化。その先の実装では `detail/reactive_socket_recv_op` の operation オブジェクトを越え、associated executor の検索チェーンを越え、その隣には 6 つのプラットフォーム I/O バックエンド（epoll、kqueue、select、dev_poll、IOCP、io_uring、さらに `null_reactor` と `spin_reactor` の 2 つのスタブ）が「どれが有効か」の判別を待って並んでいます。

問題は asio が悪く書かれていることではありません——工業水準のライブラリであり、あの階層にはそれぞれ存在理由があります。問題は、**理解したい「非同期モデル」と、それをポータブルかつ拡張可能にする「汎用化の機械」が絡み合っている**ことです。モデル自体は 2,000〜3,000 行で説明できます。残りは、モデルを 6 つのプラットフォームで動かし、任意の completion token を受けられるようにするための機械です。

だから本書は逆方向から進みます。まずモデルを 4,200 行の実装の中で読み抜き、その理解を持って本物を読みに行く。この小さな実装が **asio-simplified**（以下 sasio）です。このリポジトリが本書の付属実装です。

## 本書のコードの読み方

sasio のトレードオフは一言です：**エンジンを残し、合成層を切る**。`io_context`、handlerキュー、reactor、timer heap、strand、合成操作、UDP、resolver、C++20 coroutine——asio の幹はすべて揃っています。executor 体系も completion token もプラットフォーム分岐もなし（Windows + `WSAPoll` のみ）、IPv6 もありません。切り取った箇所にはすべて文書があり、「なぜ切ったか」「asio のどこにあるか」が書かれています。各章末の対照表はその文書の要約です。

読む方法は、どんなソースコードの読み方とも同じで、3 ステップです：

1. **1 つの操作をトレースする**：完全な呼び出しチェーン（たとえば `async_read_some` の発起から handler 実行まで）を端から端まで辿り、各ステップで誰が誰を呼び、データがどこにあるかを記録する；
2. **コードを変えて検証する**：トレースで生じた推測には、1 行変えてテストを走らせて裏付けるか覆す。sasio なら変更の心理的負担はゼロに近い；
3. **asio で対応物を見つける**：sasio でモデルを理解したら、asio の同名ファイルを開く——クラス名も階層も揃っているので、すべての概念が見覚えがあるはずです。テンプレートで包まれているだけです。

ステップ 3 が成立するのは、sasio が「asio に似た何か」ではなく、asio の**設計**通りに書かれているからです：`io_context` は同じくサービスコンテナ、`scheduler` は同じくワークカウントで停止を決め、reactor は同じく interrupter で起こされる。差分はすべて意図的で、一つ残らず台帳に記録されています。

全書は 14 章、各章は固定の 6 パート：まず「なければどうなるか」、次にデータ構造と多くとも 3 つの不変量、そして 1 つの操作の一生をトレースし、最も重要なコードを精読し、asio と対照し、最後に実験 1 つとバグハント 1 つを残します。この構成を暗記する必要はありません——同じ思考プロセスの反復訓練です。

## 全書のルート

sasio のコンポーネントの依存関係は一本の線で、本書はその線に沿って進みます：

```text
io_context ── service registry (ch3)
    │
    ├─ scheduler ──── handler queue, work count (ch4)
    │
    ├─ reactor ────── WSAPoll, descriptor table, wake-ups (ch5)
    │
    ├─ timer_queue ── min-heap, timers (ch6)
    │
    ├─ socket/acceptor ── initiate and complete, two phases (ch7)
    │
    ├─ composed operations ── async_read / async_write (ch8)
    │
    ├─ UDP / resolver ── two reuses of one engine (ch9)
    │
    ├─ strand ─────── serialisation (ch10)
    │
    └─ awaitable ───── C++20 coroutines (ch11)
```

最初の 2 章は依存チェーン上にありませんが、なければチェーンの読みが揺らぎます：第 2 章は reactorとproactorのモデルを立て、全書で繰り返し引用される 3 つのエンジン契約を提示します。第 12〜14 章は asio に戻ります：切り取られた 2 層（executor と completion token）がどう働くか、本物の asio をどう読むか。

各コンポーネントには自前のテストスイートが付きます——sasio の 9 つのテストファイル（`tests/asio/`）は `asio/src/tests/unit/` から移植され、アサーションとコメントは可能な限り原文のままです。つまり、sasio で検証した挙動は asio の挙動であり、模造品の近似ではありません。

## 最初のコード

sasio の完全なプログラムを見てください——`examples/ex01_post.cpp` の中身です：

```cpp
sasio::io_context ctx;

ctx.post([] { std::puts("handler 1"); });
ctx.post([] { std::puts("handler 2"); });
ctx.post([] { std::puts("handler 3"); });

ctx.run();
```

`post` は 3 つの handler をキューに入れて返ります。この瞬間、1 つも実行されていません——post の約束は「キューに入れる」であって「実行する」ではありません。`run()` こそが実行者です：キューから handler を 1 つずつ取り出して実行し、キューが空になり、タイマーがなく、登録されたdescriptorもない——やることがなくなれば——戻ります。プログラムは 3 行出力して終わります。

3 つの挙動に、もう一度立ち止まって見てほしいものがあります：

- `post` が返った時点で、handler は **1 つも実行されていない**。発起と実行は分かれており、この境界線は全書を貫きます；
- `run()` が戻る前提は**待ちが何もない**こと——キューが空だけでなく、保留中のタイマー、登録された descriptorも含む（その「やることがない」判定の正確な形は第 4 章で）；
- `run()` がやることを尽くして戻ったら、io_context は stopped 状態に入り、もう一度走らせるには `restart()` が必要です。これは asio の挙動で、sasio はそれをそのまま写しています。

わずかな行に、3 人の主人公が全員登場しています：**キュー**（handler はどこで待つか）、**work**（何がループを生かし続けるか）、**停止**（いつ終わりか）。これらは全書前半の核心要素で、第 3、4 章がそれぞれ受け持ちます。

## sasio と asio の体量対照

| | sasio | asio 1.38.2 |
| --- | --- | --- |
| ヘッダー数 | 18 | 600（`include/asio` 下の `.hpp`） |
| 行数 | 4,207（実測；`include/sasio` + `src` の `.hpp`/`.cpp`） | 169,063（`.ipp` を除く；含めば約 19.7 万） |
| I/O バックエンド | 1（WSAPoll） | 6（epoll / kqueue / select / dev_poll / IOCP / io_uring） |
| executor / completion token | なし | あり、API 全体に行き渡る |
| テスト | 9、asio のテストスイートから移植 | 数百 |

この表は「小さい方が大きいより良い」という意味ではなく、**読む順序**です：4,200 行には読み飛ばせる機械が 1 行もなく、169,063 行の大部分はそうです。前者を読み抜けば、後者は「知っているモデルを認識する」ことと「各層の機械がなぜ存在するか理解する」ことの 2 つに分解され、難度は急落します。

## 練習

**実験**：sasio をゼロからビルドし、テストを全走させてください。リポジトリルートで（Windows + MSVC）：

```bat
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

9 つのテストはすべて合格し、所要は約 10 秒です。Ninja がなければデフォルトジェネレーターで構いません——`cmake --build build` が MSBuild を呼びます（その場合実行ファイルは `build\Debug\` に出ます）。

**バグハント**：`tests/asio/io_context.cpp` を開き、`io_context_test` の例外セクションを見つけてください：`int` を投げる handler と正常にカウントする handler が交互に並び、外側は try/catch で `run()` をループしています。実行する前に 3 つ予測してください：

1. 最初の handler が投げた後、**その後ろに並んでいた**正常な handler は、この pass でまだ実行されるか？
2. 例外が `run()` から飛び出した時、`stopped()` は true か false か？
3. なぜループはちょうど 3 回で `break` できるのか？

予測を書き留めてください。第 4 章が `run_one_impl` を精読する時に答え合わせをします——その時、あなたはもう挙動から実装を逆算できるはずです。

---

モデル編は次章からです。まず一見問題に見えない問いから：「非同期」の非同期は一体どこにあるのか？`recv` の戻りが結果を意味するあの終わりは、どうやって取り除かれたのか？
