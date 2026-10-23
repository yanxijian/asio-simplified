# asio-simplified

**한국어** · [English](README.md) · [简体中文](README.zh-CN.md) · [日本語](README.ja.md)

<!-- translated-from: README.md sha256:b24cf5376bc3 -->

[![License: BSL-1.0](https://img.shields.io/badge/License-BSL--1.0-blue.svg)](LICENSE_1_0.txt)
[![C++](https://img.shields.io/badge/C%2B%2B-17%20%2F%2020-blue.svg)]()
[![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()
[![Status: educational](https://img.shields.io/badge/Status-educational-orange.svg)]()

비동기 I/O 라이브러리가 어떻게 동작하는지 공부하기 위한
[Asio](https://github.com/chriskohlhoff/asio)의 작고 읽기 쉬운 부분 집합입니다.

> **비공식 프로젝트.** 이 프로젝트는 Asio 프로젝트나 그 저자와 제휴, 보증, 후원 관계가 없습니다.
> 실제 제품에서는 Asio 자체를 사용하세요.

## 왜 만들었나

Asio 1.38.2는 헤더 588개, 약 16만 4천 줄입니다. 8개의 플랫폼 백엔드, 실행자 프레임워크, 완료
토큰 디스패치, C++03 호환 계층까지 갖추고 있습니다. 설계는 훌륭하지만 그 기계 장치 때문에 설계가
잘 보이지 않습니다.

asio-simplified는 모델은 남기고 기계 장치는 덜어냅니다. Windows 전용, `WSAPoll`, 약 4,100줄,
외부 의존성 없음. 파일 이름과 타입 이름은 Asio에 맞춰 두어 나란히 읽을 수 있습니다. C++ 네임스페이스와
CMake 타깃은 `sasio`입니다.

## 구성

| 장 | 추가되는 것 | Asio 대응 |
| --- | --- | --- |
| 1 | `execution_context`, `io_context`, `scheduler`, `reactor`, `timer_queue`, `steady_timer` | `detail/scheduler`, `detail/select_reactor`, `detail/timer_queue` |
| 2 | `buffer`, `detail/socket_ops`, `ip/tcp` (`socket`, `acceptor`) | `basic_socket*`, `detail/socket_ops` |
| 3 | `strand` (핸들러 직렬화) | `strand`, `detail/strand_service` |
| 4 | 합성 연산 `async_read` / `async_write` | `read.hpp`, `write.hpp`, `detail/read_op` |
| 5 | `ip/udp`, `ip/resolver` | `ip/udp.hpp`, `ip/basic_resolver.hpp` |
| 6 | C++20 `awaitable`, `co_spawn` | `awaitable.hpp`, `co_spawn.hpp`, `use_awaitable` |

## 핵심 아이디어

- **`io_context`는 서비스 컨테이너입니다.** 스케줄러, 리액터, 타이머 큐, 스트랜드, 소켓 서비스가
  `execution_context` 안에 살고 필요할 때 생성됩니다.
- **핸들러는 항상 큐를 거칩니다.** `post()`는 넣기만 하고 `run()`이 꺼냅니다. 큐에 들어간 핸들러는
  각각 작업 단위 하나를 가지며, 카운트가 0이 되면 컨텍스트가 스스로 멈춥니다. 그래서 두 번째
  `run()` 전에는 `restart()`가 필요합니다.
- **리액터가 프로액터를 흉내 냅니다.** `WSAPoll`이 준비 상태를 알리면 그때 소켓 연산이
  `recv`/`send`/`accept`/`connect`를 수행하고 완료 핸들러를 게시합니다. 폴링 집합은 스냅샷이라서
  새로운 관심이나 더 이른 타이머는 대기를 깨워야 합니다. 그렇지 않으면 다른 스레드에서 시작한
  연산이 다음 `post`까지 계속 잠들어 있게 됩니다.
- **방향마다 연산 하나.** 소켓은 읽기 슬롯과 쓰기 슬롯을 하나씩만 가지며, 같은 방향으로 두 번째
  연산을 시작하면 `already_started`가 됩니다.
- **strand, 합성 연산, 코루틴.** strand는 큐와 실행 중 플래그이고, 합성 연산은 `*_some` 호출을 잇는
  `shared_ptr` 상태 기계이며, `awaitable`은 대칭 전이(symmetric transfer)로 기다리던 코루틴을
  재개합니다.

## 빌드와 테스트

Windows, MSVC(또는 임의의 C++17 컴파일러), CMake 3.20 이상, Ninja가 필요합니다. 코루틴 테스트는
C++20으로 컴파일되지만 라이브러리 자체는 C++17입니다.

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

예제:

```bat
build\ex01_post.exe
build\ex02_timer.exe
build\ex03_echo_server.exe 5555
```

## 테스트

테스트 실행 파일은 9개입니다. Asio에 알맞은 단위 테스트가 있으면 그대로 이식하고 원래 저작권
표시를 유지했습니다: `io_context`, `deadline_timer`(`steady_timer`로), `strand`, `read`, `write`,
그리고 `ip/tcp`의 `no_delay` 부분. Asio에는 원시 소켓 I/O, UDP, 코루틴에 대한 런타임 단위 테스트가
없어서 해당 사례는 예제와 문서화된 동작에서 만들었고 파일 머리말에 그 사실을 적었습니다. 단순화
API에 맞지 않는 부분은 `OMITTED`로 표시했습니다.

Debug CRT 어서션은 즉시 실패로 바꾸었습니다. 깨진 불변 조건을 "Ignore"로 넘겨서 성공으로 보고하는
일은 할 수 없습니다.

## 의도적인 제한

- Windows와 `WSAPoll`만 지원합니다. IOCP 백엔드는 없습니다(선택 부록은 구현하지 않음).
- IPv4 TCP와 UDP만 지원합니다. IPv6, SSL, 로컬 소켓, 시리얼 포트는 없습니다.
- 실행자 프레임워크, 완료 토큰, `any_io_executor`는 없습니다.
- 방향마다 미완료 연산 하나, 연산마다 버퍼 하나, 버퍼 시퀀스는 없습니다.
- 리졸버는 질의마다 짧게 사는 스레드를 하나 시작합니다. `io_context`는 미완료 질의보다 오래
  살아 있어야 합니다.
- 객체는 스레드 안전하지 않습니다. 대기 중인 소켓을 다른 스레드에서 `close()`하지 마세요.
- Asio의 drop-in 대체가 아니며 실제 제품용도 아닙니다.

## 읽는 순서

1. `include/sasio/execution_context.hpp` — `io_context`가 왜 이렇게 얇은지.
2. `src/scheduler.cpp`의 `run_one_impl()` — 큐/작업/타임아웃 루프.
3. `src/reactor.cpp` — `WSAPoll`, 디스크립터 표, 인터럽터.
4. `src/timer_queue.cpp`, `src/steady_timer.cpp` — 힙과 기상 규칙.
5. `include/sasio/ip/tcp.hpp`, `src/ip_tcp.cpp` — 시작, 등록, 완료.
6. `src/strand.cpp`, `include/sasio/read.hpp`, `include/sasio/awaitable.hpp` — 직렬화, 합성,
   코루틴.

## 라이선스와 출처

[Boost Software License 1.0](LICENSE_1_0.txt)으로 배포됩니다. Asio에서 이식한 테스트 자료의
저작권은 Christopher M. Kohlhoff(2003-2026)에게 있으며 같은 라이선스로 사용합니다. 자세한 내용은
[NOTICE](NOTICE)를 참고하세요.
