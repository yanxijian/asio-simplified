<!-- translated-from: book/src/ch06-timer.md sha256:d7df90e38a1f -->

# 時間：timer_queue と steady_timer

run ループはタイマーキューに 2 つの問いを投げます：「次はいつ期限切れか」（どれだけ
眠るか）、「今、期限切れはあるか」（仕事をするかどうか）。第 5 章の起床経路には
もう 1 つ：「新しく来たタイマーは最早か」（起こすかどうか）。3 つすべてに答えるのが
`timer_queue` で、データ構造は教科書どおりの最小ヒープ——しかしこの 140 行足らずの
ファイルには、asio 自家のテストスイートが捕まえた、本書で唯一の本物のバグが埋まって
います。この章で分解します。

## 設計：ヒープ、インデックス、生存期間

保留中のタイマーは `timer_queue` の中では 1 つの `entry` です：

```cpp
struct entry
{
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  clock::time_point when;                    // expiry moment
  std::function<void(error_code)> handler;   // who to call
  std::size_t heap_index = npos;             // my position in the heap
};
```

ヒープ本体は `std::vector<std::shared_ptr<entry>>` で、`when` で並んだ最小ヒープ
です。不変量は 2 つ：

1. **ヒープ順が成立し、ヒープ内のすべての entry の `heap_index` が実際の位置と
   一致している**；ヒープにない entry の `heap_index` は必ず `npos`。この 2 つは
   どちらも欠かせません——前者は `front()` が常に最早の期限であることを保証し、
   後者は「キャンセル」が古いインデックスでヒープを勝手に触らないことを
   保証します；
2. **キャンセルされた完了は、その場で呼ばれることはない**。`cancel()` がどの
   呼び出しスタックからでも（他の handler のスタック内でも）呼ばれて、キャンセル
   された handler は `post` 経由でのみ届きます——再入は非同期コードで最も陰湿な
   地雷であり、この不変量がそれを取り除きます。

`entry` は `shared_ptr` で保持されます。なぜなら、**同時に 2 つの世界に生きている**から
です：ヒープに 1 つの参照、タイマーオブジェクト（`steady_timer`）の `pending_` に
1 つの参照。先に動く者が運命を決めます——`cancel()` が先にロックを取れば handler を
引き出してキャンセルを配送し、`take_expired` が先なら handler を引き出して
成功を配送します。互斥ロックがこの競合の窓を原子まで狭め、どちらの側も
「半死半生」の状態を見ません。

## 精読：pop_at と本物のバグ

ヒープの核心操作は「任意の位置の要素を削除する」こと。ヒープ頂点の削除
（`take_expired` が `pop_at(0)` を呼ぶ）とキャンセル（`remove` が `pop_at(任意位置)`
を呼ぶ）は同じコードを共有します：

```cpp
void timer_queue::pop_at(std::size_t index)
{
  if (index >= heap_.size())
    return;

  const std::shared_ptr<entry> removed = heap_[index];
  const std::size_t last = heap_.size() - 1;

  // Move the last element into the hole. Do not use swap_at() here: it would
  // write the removed entry's heap_index back to a valid-looking value, which
  // is exactly the stale index that made a later remove() index out of bounds.
  if (index != last)
  {
    heap_[index] = std::move(heap_[last]);
    heap_[index]->heap_index = index;
  }

  heap_.pop_back();
  removed->heap_index = entry::npos;      // ← the fix: leaving the heap is npos

  if (index < heap_.size())
  {
    sift_down(index);
    sift_up(index);
  }
}
```

このコメントの「Do not use swap_at() here」が本章の核心です。ヒープの教科書的な
削除の書き方は：最後の要素を穴の位置に移し、`pop_back` してから、新しい位置で
上下に 1 回ずつ篩う。手を抜いて既存の `swap_at` を流用すると：

```cpp
// the buggy version:
swap_at(index, last);    // swap_at rewrites BOTH entries' heap_index!
heap_.pop_back();
```

`swap_at` の職責はヒープ順の維持で、忠実に**両方**の要素のインデックスを更新します
——削除されようとしている entry も含めて。削除後、`removed->heap_index` はもう
合法でない位置を指します。具体的な数字で見ます：ヒープに 2 つのタイマー、頂点を
キャンセル。`pop_at(0)`：`index=0`、`last=1`、交換後、削除された entry の
`heap_index` は 1 に；`pop_back` の後、ヒープのサイズは 1 に。この時
`removed->heap_index == 1 == heap_.size()`——範囲外の値が確定します。

いつ爆発するのか？この entry は `steady_timer` の `pending_` が参照を持ち、タイマー
オブジェクトのデストラクタが `cancel()` を呼び、`cancel` が `remove` を呼び、
`remove` は `heap_index = 1` でサイズ 1 のヒープを引く——`heap_[1]` は範囲外で、
Debug 版 STL のアサーションがその場で飛び出します
（`vector subscript out of range`）。

なぜこのバグは生き残れたのか？**2 つ**のタイマーが必要だからです：タイマーが
1 つなら `index == last`、交換なし、越界なし、ヒープを空にしても何も起きません。
当初の自作テストはちょうどタイマーを 1 つしか使っていませんでした。asio 自家の
`deadline_timer` テストを移植した時——用例が 2 つのタイマーを並べて置いた——
Debug アサーションが一発で飛び出しました。これが本書の「テスト移植が本物のバグを
捕まえた」の由来です。修復は 2 段構えです：`pop_at` は `swap_at` を
使わず、ヒープを出たら `npos` を置く（根本原因）；`remove` の冒頭でインデックスに
**境界 + 身元**の二重チェック——

```cpp
const std::size_t index = e->heap_index;
if (index == entry::npos || index >= heap_.size() || heap_[index] != e)
{
  e->heap_index = entry::npos;
  return false;
}
```

仮に将来また汚れたインデックスが現れても、`remove` は静かに「ヒープにない」と
返すだけで、範囲外に届きません。根本原因の修復と、防御的な検証の 2 段構えで
それぞれの任務を果たします。

同じ `add` は第 5 章の起床信号も兼ねます——「新しい entry が最早になったか」を
返します：

```cpp
const bool earliest = heap_.empty() || e->when < heap_.front()->when;
// ...push, sift up...
return earliest;      // steady_timer decides on wake_waiters() from this
```

1 つの戻り値が「スナップショットモデルの 2 回目の失守」の修復をヒープの内部から
きれいに引き出します——ヒープ自身は reactor の存在を知らず、ロック順
（scheduler → timer → reactor）も破られません。

## steady_timer：待ち、キャンセル、破棄

ヒープはエンジンの部品で、`steady_timer` はユーザーが手にするハンドルです。
3 つの操作を 1 つずつ見ます。

**`async_wait`**：`{expiry_, handler}` を entry にまとめてヒープに掛けます。1 つの
タイマーオブジェクトは同時に 1 つの待ちしか許しません——2 回目の `async_wait` は
まず前の 1 つを `cancel` します（古い待ちは `operation_aborted` を携えて完了し、
新しい待ちがヒープに入ります）。asio は同じオブジェクトに任意の数の待ちを許しますが、
これが sasio の差異表に載っている簡略化です。

**`cancel`**：`pending_` の参照を引き取って（`std::move`）、ヒープに削除を要求します。
削除が成功すれば、待ちはまだ期限切れになっていない——handler を entry から
引き出し、`post` で `operation_aborted` を携えた完了を投げます：

```cpp
// Never invoke the handler from inside cancel(): post it, exactly like asio.
scheduler_.post([handler = std::move(handler)]() mutable {
  handler(operation_aborted);
});
```

削除が失敗すれば、待ちが**すでに期限切れで、完了がすでに路上にある**ことを
意味します（scheduler の `take_expired` が handler を引き取った）——`cancel` は 0 を
返し、何もしません。この判定のロック規律に注目：`take_expired` が handler を
entry から引き出すことと `cancel` の `remove` は、同じ `timer_queue::mutex_` で
直列化されるため、「両方が handler を得る」ことも「どちらも得ない」ことも
ありません。不変量 2 はここで果たされます：`cancel` がどのスタックから発起されても、
handler は `post` を経由して run ループ内で実行され、絶対にその場で再入しません。

**破棄**：`~steady_timer` は `cancel()` です——オブジェクトが死ぬと、保留中の待ちが
`operation_aborted` を携えて完了します。asio の意味論も同じです。代償は、「まだ
待っている」タイマーの破棄が完了 handler をキューに注入することなので、
**破棄のタイミング**には心の準備が要ります（だから co_spawn のアダプターはタイマーを
参照で持つ——coroutineが生きているなら、タイマーも生きていなければなりません）。

## 対照表

| 問題 | asio | sasio |
| --- | --- | --- |
| ヒープ本体 | `detail/timer_queue.hpp`（クロック型ごとに 1 つ） | `src/timer_queue.cpp`（steady_clock のみ） |
| タイマーの身元 | `per_timer_data`（タイマーごとに待ちリンクリスト） | `shared_ptr<entry>`（オブジェクトごとに高々 1 つの待ち） |
| 多重待ち | 対応；`expires_at` がすべてキャンセル | 未対応；新しい待ちが古い待ちを自動キャンセル |
| キャンセルの粒度 | `cancel` / `cancel_one` | `cancel` |
| 完了の経路 | 完了 op が scheduler のキューへ | 期限は scheduler のループ内で直接呼び出し；キャンセルは `post` 経由 |
| reactor との関係 | タイマーキューは reactor に登録され、reactor が poll タイムアウトを計算 | タイマーキューは独立した service、scheduler が poll タイムアウトを計算 |

2 つの差異をもう少し説明します。1 つ目、asio は timer queue を reactor に掛け、
reactor が poll タイムアウトを計算しますが、sasio は scheduler に掛けて scheduler が
計算します——位置は違いますが、「タイムアウトはスナップショットであり、新しい最早
タイマーは起こさなければならない」という教訓は両側で学ばれました（asio の
`schedule_timer` interrupt、sasio の `wake_waiters`）。2 つ目、多重待ちの欠落は
手抜きではなく、その実装価値は「1 つのタイマーオブジェクトを複数の待ちの共通
ハンドルとして使う」ことにあり、sasio のcoroutineアダプター（第 11 章）はそう
使いません——各 co_await に独立したタイマーで、単一の待ちで十分です。

## 練習

**実験**：`pop_at` の範囲外バグを手で再現します。`src/timer_queue.cpp` を開き、
`pop_at` をコメントが描写する旧実装に戻します（`swap_at(index, last)` を使い、
`removed->heap_index = entry::npos;` の行を削除）。さらに 10 行の 2 タイマー
用例を書きます：

```cpp
sasio::io_context ctx;
sasio::steady_timer t1(ctx), t2(ctx);
t1.expires_after(std::chrono::milliseconds(50));
t1.async_wait([](sasio::error_code) {});
t2.expires_after(std::chrono::milliseconds(300));
t2.async_wait([](sasio::error_code) {});
// t1 expires → pop_at(0) takes the swap path → the other entry's index goes dirty
// t1/t2 destruct → cancel → remove reads the dirty index → Debug assert fires
```

Debug 版でビルド・実行し、アサーションが飛び出す**時機と呼び出しスタック**を
見てください——爆発の位置は `pop_at` ではなく、**その後**のどこかの `remove` の
`heap_[index]` です。正しい実装に戻すと、アサーションは消えます。この練習が
くれるのは一種の感覚です：「インデックスと位置が常に一致していなければならない」
構造では、汚れたインデックスの爆発点は、常に犯行現場から遠いところです。

**バグハント**：`steady_timer::cancel` の中で、`timers_.remove(entry)` が false を
返すと、そのまま `return 0` します——何も配送しません。しかしこの時 entry の
handler はまだ entry の中にあり、entry は `pending_` と一緒にまもなく捨てられる
かもしれません。handler は失われて、永遠に呼ばれないのでしょうか？（ヒント：
`remove` が失敗するのは 3 つの理由だけ——インデックスが npos、範囲外、
身元不一致。1 つずつ考えます：それぞれの場合、handler の完了は誰が責任を持つのか？
答えは `take_expired` がロックを取る時機の中にあります。）

---

キュー、準備完了、時間——エンジンの三大部品がすべて分解し終え、起床経路の
修復もすべて所定の位置にあります。次章から、このエンジンで本当に役に立つことを
します：`socket` の `async_read_some` は発起時に「状態を保存、関心を登録」し、
準備ができたら `do_read` がシステムコールに触れる——発起関数と完了経路の
2 相式は、asio のすべての socket コードの共通の形です。
