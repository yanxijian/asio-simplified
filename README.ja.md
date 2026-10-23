# asio-simplified

**日本語** · [English](README.md) · [简体中文](README.zh-CN.md) · [한국어](README.ko.md)

<!-- translated-from: README.md sha256:b24cf5376bc3 -->

[![License: BSL-1.0](https://img.shields.io/badge/License-BSL--1.0-blue.svg)](LICENSE_1_0.txt)
[![C++](https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-blue.svg)]()
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()
[![Status: educational](https://img.shields.io/badge/Status-educational-orange.svg)]()

非同期 I/O ライブラリがどのように動くかを学ぶための、[Asio](https://github.com/chriskohlhoff/asio)
の小さくて読めるサブセットです。

> **非公式プロジェクト。** 本プロジェクトは Asio プロジェクトおよびその作者と提携・承認・後援の
> 関係にありません。製品では Asio 本体を使用してください。

## なぜ作ったか

Asio 1.38.2 は 588 個のヘッダー、約 16.4 万行あります。8 つのプラットフォームバックエンド、
エグゼキューター機構、完了トークンのディスパッチ、C++03 互換レイヤーを備えています。設計は
優れていますが、その機構のせいで設計が見えにくくなっています。

asio-simplified はモデルを残し、機構を削ります。Windows 専用、`WSAPoll`、約 4,100 行、外部依存
なし。ファイル名と型名は Asio に合わせてあるので、両者を並べて読めます。C++ の名前空間と
CMake ターゲットは `sasio` です。

## 内容

| 章 | 追加されるもの | Asio の対応物 |
| --- | --- | --- |
| 1 | `execution_context`、`io_context`、`scheduler`、`reactor`、`timer_queue`、`steady_timer` | `detail/scheduler`、`detail/select_reactor`、`detail/timer_queue` |
| 2 | `buffer`、`detail/socket_ops`、`ip/tcp`（`socket`、`acceptor`） | `basic_socket*`、`detail/socket_ops` |
| 3 | `strand`（ハンドラーの直列化） | `strand`、`detail/strand_service` |
| 4 | 合成操作 `async_read` / `async_write` | `read.hpp`、`write.hpp`、`detail/read_op` |
| 5 | `ip/udp`、`ip/resolver` | `ip/udp.hpp`、`ip/basic_resolver.hpp` |
| 6 | C++20 の `awaitable`、`co_spawn` | `awaitable.hpp`、`co_spawn.hpp`、`use_awaitable` |

## 中心となる考え方

- **`io_context` はサービスの入れ物。** スケジューラー、リアクター、タイマーキュー、ストランド、
  ソケットサービスは `execution_context` の中にあり、必要になったときに生成されます。
- **ハンドラーは必ずキューを通る。** `post()` は入れるだけで、`run()` が取り出します。キュー上の
  ハンドラーはそれぞれ作業単位を 1 つ持ち、カウントが 0 になるとコンテキストは自分で停止します。
  そのため 2 回目の `run()` には `restart()` が必要です。
- **リアクターがプロアクターを演じる。** `WSAPoll` がレディネスを報告し、その後にソケット操作が
  `recv`/`send`/`accept`/`connect` を実行して完了ハンドラーを投函します。ポーリング集合は
  スナップショットなので、新しい関心やより早いタイマーは待機を中断させる必要があります。
  そうしないと、別スレッドから開始した操作は次の `post` まで眠り続けます。
- **方向ごとに 1 操作。** ソケットは読み書きそれぞれ 1 スロットだけを持ち、同じ方向で 2 つ目を
  開始すると `already_started` になります。
- **ストランド、合成操作、コルーチン。** ストランドはキューと実行中フラグ、合成操作は `*_some` を
  つなぐ `shared_ptr` の状態機械、`awaitable` は対称転送で待機中のコルーチンを再開します。

## ビルドとテスト

Windows、MSVC（または任意の C++17 コンパイラー）、CMake 3.20 以上、Ninja が必要です。コルーチンの
テストは C++20 でコンパイルされますが、ライブラリ本体は C++17 です。

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

サンプル：

```bat
build\ex01_post.exe
build\ex02_timer.exe
build\ex03_echo_server.exe 5555
```

## テスト

テスト実行ファイルは 9 個です。Asio に適切な単体テストがある場合はそこから移植し、元の著作権
表示を残しています：`io_context`、`deadline_timer`（`steady_timer` として）、`strand`、`read`、
`write`、`ip/tcp` の `no_delay` 部分。Asio には生のソケット I/O、UDP、コルーチンの実行時単体
テストがないため、それらは示例と文書化された動作から作られ、ファイル先頭にその旨を記しています。
簡略化 API に合わない部分は `OMITTED` と明記しています。

Debug CRT のアサーションは即時失敗に変えてあります。壊れた不変条件を「Ignore」で飛ばして
成功扱いにする、ということはできません。

## 意図的な制限

- Windows と `WSAPoll` のみ。IOCP バックエンドはありません（任意の付録は未実装）。
- IPv4 の TCP と UDP のみ。IPv6、SSL、ローカルソケット、シリアルポートはありません。
- エグゼキューター機構、完了トークン、`any_io_executor` はありません。
- 方向ごとに 1 つの未完了操作、操作ごとに 1 つのバッファー、バッファー列はありません。
- リゾルバーは問い合わせごとに短命スレッドを 1 つ起動します。`io_context` は未完了の問い合わせより
  長く生きている必要があります。
- オブジェクトはスレッド安全ではありません。待機中のソケットを別スレッドから `close()` しないで
  ください。
- Asio の drop-in 代替ではなく、製品利用も想定していません。

## 読む順序

1. `include/sasio/execution_context.hpp` — `io_context` がなぜこんなに薄いのか。
2. `src/scheduler.cpp` の `run_one_impl()` — キュー・作業・タイムアウトのループ。
3. `src/reactor.cpp` — `WSAPoll`、ディスクリプター表、インターラプター。
4. `src/timer_queue.cpp`、`src/steady_timer.cpp` — ヒープと起床の規則。
5. `include/sasio/ip/tcp.hpp`、`src/ip_tcp.cpp` — 開始、登録、完了。
6. `src/strand.cpp`、`include/sasio/read.hpp`、`include/sasio/awaitable.hpp` — 直列化、合成、
   コルーチン。

## ライセンスと帰属

[Boost Software License 1.0](LICENSE_1_0.txt) の下で配布されます。Asio から移植したテスト材料の
著作権は Christopher M. Kohlhoff（2003-2026）にあり、同じライセンスで使用しています。詳細は
[NOTICE](NOTICE) を参照してください。
