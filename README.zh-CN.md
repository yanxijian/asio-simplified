# asio-simplified

**ç®€ä½“ä¸­æ–‡** Â· [English](README.md) Â· [æ—¥æœ¬èªž](README.ja.md) Â· [í•œêµ­ì–´](README.ko.md)

[![CI](https://github.com/yanxijian/asio-simplified/actions/workflows/ci.yml/badge.svg)](https://github.com/yanxijian/asio-simplified/actions/workflows/ci.yml)
[![License: BSL-1.0](https://img.shields.io/badge/License-BSL--1.0-blue.svg)](LICENSE_1_0.txt)
[![C++](https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-blue.svg)]()
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()
[![Status: educational](https://img.shields.io/badge/Status-educational-orange.svg)]()

<!-- translated-from: README.md sha256:725f0014e8d1 -->

[Asio](https://github.com/chriskohlhoff/asio) çš„ä¸€ä¸ªå°è€Œå¯è¯»çš„å­é›†ï¼Œç”¨æ¥å­¦ä¹ å¼‚æ­¥ I/O åº“æ˜¯å¦‚ä½•å·¥ä½œçš„ã€‚

> **éžå®˜æ–¹é¡¹ç›®ã€‚** æœ¬é¡¹ç›®ä¸Ž Asio é¡¹ç›®åŠå…¶ä½œè€…æ²¡æœ‰éš¶å±žã€èƒŒä¹¦æˆ–èµžåŠ©å…³ç³»ã€‚ç”Ÿäº§çŽ¯å¢ƒè¯·ç›´æŽ¥ä½¿ç”¨ Asioã€‚

## ä¸ºä»€ä¹ˆ

Asio 1.38.2 æœ‰ 588 ä¸ªå¤´æ–‡ä»¶ã€çº¦ 16.4 ä¸‡è¡Œï¼š8 å¥—å¹³å°åŽç«¯ã€executor æ¡†æž¶ã€completion token åˆ†æ´¾ï¼Œ
ä»¥åŠ C++03 å…¼å®¹å±‚ã€‚è®¾è®¡å¾ˆå¥½ï¼Œä½†æœºæ¢°ç»“æž„æŠŠè®¾è®¡é®ä½äº†ã€‚

asio-simplified ä¿ç•™æ¨¡åž‹ã€åŽ»æŽ‰æœºæ¢°ç»“æž„ï¼šåªåš Windowsã€åªç”¨ `WSAPoll`ã€çº¦ 4100 è¡Œã€æ— ç¬¬ä¸‰æ–¹ä¾èµ–ï¼Œ
æ–‡ä»¶åå’Œç±»åž‹åä¸Ž Asio å¯¹é½ï¼Œå¯ä»¥å·¦å³å¯¹ç…§ç€è¯»ã€‚C++ åå­—ç©ºé—´ä¸Ž CMake target éƒ½æ˜¯ `sasio`ã€‚

## å†…å®¹

| ç«  | æ–°å¢žå†…å®¹ | Asio å¯¹ç…§ |
| --- | --- | --- |
| 1 | `execution_context`ã€`io_context`ã€`scheduler`ã€`reactor`ã€`timer_queue`ã€`steady_timer` | `detail/scheduler`ã€`detail/select_reactor`ã€`detail/timer_queue` |
| 2 | `buffer`ã€`detail/socket_ops`ã€`ip/tcp`ï¼ˆ`socket`ã€`acceptor`ï¼‰ | `basic_socket*`ã€`detail/socket_ops` |
| 3 | `strand`ï¼ˆhandler ä¸²è¡ŒåŒ–ï¼‰ | `strand`ã€`detail/strand_service` |
| 4 | ç»„åˆæ“ä½œ `async_read` / `async_write` | `read.hpp`ã€`write.hpp`ã€`detail/read_op` |
| 5 | `ip/udp`ã€`ip/resolver` | `ip/udp.hpp`ã€`ip/basic_resolver.hpp` |
| 6 | C++20 `awaitable`ã€`co_spawn` | `awaitable.hpp`ã€`co_spawn.hpp`ã€`use_awaitable` |

## æ ¸å¿ƒæ€æƒ³

- **`io_context` æ˜¯ä¸€ä¸ªæœåŠ¡å®¹å™¨ã€‚** è°ƒåº¦å™¨ã€reactorã€å®šæ—¶å™¨é˜Ÿåˆ—ã€strand å’Œ socket æœåŠ¡éƒ½ä½åœ¨
  `execution_context` é‡Œï¼ŒæŒ‰éœ€åˆ›å»ºã€‚
- **I/O å®Œæˆèµ°é˜Ÿåˆ—ï¼›åˆ°æœŸå®šæ—¶å™¨ç”± run å¾ªçŽ¯ç›´æŽ¥è°ƒç”¨ã€‚** `post()` åªå…¥é˜Ÿï¼Œ`run()` æ‰æŽ’ç©ºã€‚æ¯ä¸ªå…¥é˜Ÿ
  handler å ä¸€ä¸ª work å•ä½ï¼Œè®¡æ•°ä¸Žä¸‰ç±»å¾…åŠžï¼ˆé˜Ÿåˆ—ã€å®šæ—¶å™¨ã€æè¿°ç¬¦ï¼‰å…¨ç©ºæ—¶ context æ‰åœæ­¢ï¼Œæ‰€ä»¥
  å†æ¬¡ `run()` ä¹‹å‰å¿…é¡» `restart()`ã€‚
- **reactor æŠŠå°±ç»ªä¼ªè£…æˆå®Œæˆã€‚** `WSAPoll` æŠ¥å‘Šå°±ç»ªåŽï¼Œsocket æ“ä½œæ‰æ‰§è¡Œ
  `recv`/`send`/`accept`/`connect` å¹¶æŠ•é€’å®Œæˆ handlerã€‚poll é›†åˆæ˜¯å¿«ç…§ï¼Œæ‰€ä»¥æ–°çš„å…´è¶£å’Œæ›´æ—©çš„
  å®šæ—¶å™¨å¿…é¡»æ‰“æ–­ç­‰å¾…â€”â€”å¦åˆ™å¦ä¸€ä¸ªçº¿ç¨‹å‘èµ·çš„æ“ä½œä¼šä¸€ç›´ç¡åˆ°ä¸‹ä¸€æ¬¡ `post`ã€‚
- **æ¯ä¸ªæ–¹å‘ä¸€ä¸ªæœªå®Œæˆæ“ä½œã€‚** socket åªæœ‰ä¸€ä¸ªè¯»æ§½å’Œä¸€ä¸ªå†™æ§½ï¼›åŒæ–¹å‘å†å‘èµ·ä¼šå¾—åˆ°
  `already_started`ã€‚
- **strandã€ç»„åˆæ“ä½œã€åç¨‹ã€‚** strand æ˜¯é˜Ÿåˆ—åŠ è¿è¡Œæ ‡å¿—ï¼›ç»„åˆæ“ä½œæ˜¯ç”¨ `shared_ptr` çŠ¶æ€ä¸²èµ·å¤šæ¬¡
  `*_some` çš„çŠ¶æ€æœºï¼›`awaitable` é€šè¿‡ symmetric transfer æ¢å¤ç­‰å¾…å®ƒçš„åç¨‹ã€‚

## æž„å»ºä¸Žæµ‹è¯•

éœ€è¦ Windowsã€MSVCï¼ˆæˆ–ä»»æ„ C++17 ç¼–è¯‘å™¨ï¼‰ã€CMake 3.20+ å’Œ Ninjaã€‚åç¨‹æµ‹è¯•æŒ‰ C++20 ç¼–è¯‘ï¼Œ
åº“æœ¬èº«æ˜¯ C++17ã€‚

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

ç¤ºä¾‹ï¼š

```bat
build\ex01_post.exe
build\ex02_timer.exe
build\ex03_echo_server.exe 5555
build\ex04_bind_executor.exe
```

## æµ‹è¯•

å…± 9 ä¸ªæµ‹è¯•å¯æ‰§è¡Œæ–‡ä»¶ã€‚å‡¡æ˜¯ Asio æœ‰åˆé€‚å•æµ‹çš„ï¼Œéƒ½ä»Žå®ƒè£å‰ªè€Œæ¥å¹¶ä¿ç•™åŽŸå§‹ç‰ˆæƒå¤´ï¼š`io_context`ã€
`deadline_timer`ï¼ˆæ”¹ä¸º `steady_timer`ï¼‰ã€`strand`ã€`read`ã€`write`ï¼Œä»¥åŠ `ip/tcp` çš„ `no_delay` éƒ¨åˆ†ã€‚
Asio æ²¡æœ‰é’ˆå¯¹åŽŸå§‹ socket I/Oã€UDP å’Œåç¨‹çš„è¿è¡Œæ—¶å•æµ‹ï¼Œè¿™äº›ç”¨ä¾‹æ”¹ç”±å…¶ç¤ºä¾‹å’Œæ–‡æ¡£è¡Œä¸ºæž„é€ ï¼Œå¹¶åœ¨
æ–‡ä»¶å¤´æ³¨æ˜Žã€‚ä¸é€‚é…ç®€åŒ– API çš„éƒ¨åˆ†éƒ½æ ‡äº† `OMITTED`ã€‚

Debug CRT æ–­è¨€è¢«æ”¹æˆç«‹å³å¤±è´¥ï¼ŒåæŽ‰çš„ä¸å˜é‡ä¸å¯èƒ½é ç‚¹ä¸€æ¬¡â€œIgnoreâ€ç»§ç»­è·‘å®Œè¿˜æŠ¥å‘ŠæˆåŠŸã€‚

## æœ‰æ„çš„é™åˆ¶

- åªæ”¯æŒ Windows + `WSAPoll`ï¼›æ²¡æœ‰ IOCP åŽç«¯ï¼ˆå¯é€‰é™„å½•æœªå®žçŽ°ï¼‰ã€‚
- åªæ”¯æŒ IPv4 çš„ TCP ä¸Ž UDPï¼›æ²¡æœ‰ IPv6ã€SSLã€æœ¬åœ°å¥—æŽ¥å­—å’Œä¸²å£ã€‚
- æ²¡æœ‰ executor æ¡†æž¶ã€completion tokenã€`any_io_executor`ã€‚
- æ¯ä¸ªæ–¹å‘ä¸€ä¸ªæœªå®Œæˆæ“ä½œï¼Œæ¯ä¸ªæ“ä½œä¸€ä¸ª bufferï¼Œæ²¡æœ‰ buffer åºåˆ—ã€‚
- UDP æ•°æ®æŠ¥æˆªæ–­ä»¥é”™è¯¯æŠ•é€’ï¼›Asio åžæŽ‰ `WSAEMSGSIZE` ä»¥å¯¹é½ POSIX è¯­ä¹‰ã€‚
- resolver æ¯æ¬¡æŸ¥è¯¢èµ·ä¸€ä¸ªçŸ­å‘½çº¿ç¨‹ï¼›`io_context` å¿…é¡»æ¯”æœªå®Œæˆçš„æŸ¥è¯¢æ´»å¾—ä¹…ã€‚
- å¯¹è±¡ä¸æ˜¯çº¿ç¨‹å®‰å…¨çš„ï¼šä¸è¦åœ¨å¦ä¸€ä¸ªçº¿ç¨‹ `close()` æ­£åœ¨ç­‰å¾…çš„ socketã€‚
- ä¸æ˜¯ Asio çš„ drop-in æ›¿ä»£å“ï¼Œä¹Ÿä¸é¢å‘ç”Ÿäº§çŽ¯å¢ƒã€‚

executor å…³è”ä¸Ž completion token æ˜¯åˆ»æ„çœç•¥çš„ã€‚[Executor ä¸Ž completion token](docs/zh/executor-and-tokens.md)
è§£é‡Šå®ƒä»¬è§£å†³ä»€ä¹ˆé—®é¢˜ã€ç¼ºäº†å®ƒä»¬çš„ä»£ä»·ï¼Œä»¥åŠ `examples/ex04_bind_executor.cpp` å¦‚ä½•ç”¨ `strand` æ‰‹å·¥è¿‘ä¼¼
`bind_executor`ã€‚

## é˜…è¯»é¡ºåº

1. `include/sasio/execution_context.hpp`â€”â€”`io_context` ä¸ºä»€ä¹ˆè¿™ä¹ˆè–„ã€‚
2. `src/scheduler.cpp` çš„ `run_one_impl()`â€”â€”é˜Ÿåˆ—/work/è¶…æ—¶å¾ªçŽ¯ã€‚
3. `src/reactor.cpp`â€”â€”`WSAPoll`ã€æè¿°ç¬¦è¡¨å’Œ interrupterã€‚
4. `src/timer_queue.cpp`ã€`src/steady_timer.cpp`â€”â€”å †ä¸Žå”¤é†’è§„åˆ™ã€‚
5. `include/sasio/ip/tcp.hpp`ã€`src/ip_tcp.cpp`â€”â€”å‘èµ·ã€æ³¨å†Œã€å®Œæˆã€‚
6. `src/strand.cpp`ã€`include/sasio/read.hpp`ã€`include/sasio/awaitable.hpp`â€”â€”ä¸²è¡ŒåŒ–ã€ç»„åˆä¸Žåç¨‹ã€‚

## è®¸å¯ä¸Žå½’å±ž

ä»¥ [Boost Software License 1.0](LICENSE_1_0.txt) åˆ†å‘ã€‚æ”¹ç¼–è‡ª Asio çš„æµ‹è¯•ææ–™ç‰ˆæƒå½’
Christopher M. Kohlhoffï¼ˆ2003-2026ï¼‰ï¼ŒæŒ‰åŒä¸€è®¸å¯ä½¿ç”¨ï¼›è¯¦è§ [NOTICE](NOTICE)ã€‚
