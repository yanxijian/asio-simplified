<!-- translated-from: book/src/ch01-why.md sha256:36bdae6a16cb -->

# 4200행 버전을 먼저 읽는 이유

[chriskohlhoff/asio](https://github.com/chriskohlhoff/asio) 소스를 열어 보면,
1.38.2 기준으로 `include/asio` 디렉터리에 600개의 `.hpp` 헤더와 169,063줄이
있습니다(`.ipp` 구현 파일까지 세면 약 19.7만 줄). 그 전에 여러분은 아마 API를
쓰기만 했을 것입니다:

```cpp
asio::io_context io_context;
asio::ip::tcp::socket socket(io_context);
socket.async_read_some(buffer, handler);
io_context.run();
```

네 줄이고, 직관적으로 어렵지 않습니다. 하지만 "run()은 대체 무엇을 돌리는가",
"handler는 누구고, 어느 스레드에서, 언제 호출되는가"에 답하려고
`async_read_some`을 따라 들어가면, 첫 화면에 나오는 것은 `basic_stream_socket`의
템플릿 선언 — 프로토콜과 executor로 두 겹의 매개변수화. 그 아래 구현으로 들어가면
`detail/reactive_socket_recv_op` operation 객체를 지나, associated executor
조회 체인을 지나고, 옆에는 6개의 플랫폼 I/O 백엔드(epoll, kqueue, select,
dev_poll, IOCP, io_uring — 그리고 `null_reactor`와 `spin_reactor` 스텁 2개)가
"어느 것이 활성인가"를 분별해 달라고 기다리고 있습니다.

문제는 asio가 잘못 쓰였다는 것이 아닙니다 — 산업 수준의 라이브러리이고, 그
계층들에는 각각 존재 이유가 있습니다. 문제는 **이해하려는 "비동기 모델"과, 그
모델을 이식 가능하고 확장 가능하게 만드는 "일반화 기계"가 뒤엉켜 있다는 것**입니다.
모델 자체는 2~3천 줄이면 끝납니다. 나머지는 그 모델을 6개 플랫폼에서 돌리고,
임의의 completion token을 받아들이게 하기 위한 기계입니다.

그래서 이 책은 반대 방향으로 갑니다. 먼저 모델을 4200행짜리 구현 안에서 완전히
이해하고, 그 이해를 들고 진짜로 갑니다. 이 작은 구현이 **asio-simplified**
(약칭 sasio)입니다. 이 책과 함께 제공되는 저장소입니다.

## 이 책이 코드를 읽는 방법

sasio의 트레이드오프는 한 문장입니다: **엔진은 남기고, 조합 계층은 자른다**.
`io_context`, handler 큐, reactor, 타이머 힙, strand, 합성 연산, UDP, resolver,
C++20 coroutine — asio의 전체 몸통이 있습니다. executor 체계, completion token,
플랫폼 분기(Windows + `WSAPoll`만), IPv6는 없습니다. 자른 곳마다 문서가
있습니다: 왜 잘랐는지, asio의 어디에서 찾을 수 있는지. 각 장 끝의 대조표가 그
문서의 요약입니다.

코드를 읽는 방법도, 어떤 소스 코드의 읽기 방법과 마찬가지로 3단계입니다:

1. **연산 하나를 트레이스합니다**: 완전한 호출 사슬(예: `async_read_some`의
   발기부터 handler 실행까지)을 처음부터 끝까지 따라가며, 각 단계에서 누가 누구를
   호출하고 데이터가 어디에 있는지 기록합니다;
2. **코드를 고쳐서 검증합니다**: 트레이스에서 생긴 추측마다 한 줄을 고치고
   테스트를 돌려 확인하거나 반박합니다. sasio는 충분히 작아서 고치는 심리적
   부담이 없습니다;
3. **asio에서 대응물을 찾습니다**: sasio에서 모델을 이해했으면, asio의
   같은 이름 파일을 엽니다 — 클래스 이름과 계층이 맞춰져 있으므로 모든 개념이
   알아볼 수 있는 것입니다. 템플릿 옷을 입고 있을 뿐입니다.

3단계가 성립하는 이유는, sasio가 "asio와 비슷한 무언가"가 아니라 asio의
**설계**대로 쓰였기 때문입니다: `io_context`는 마찬가지로 서비스 컨테이너,
`scheduler`는 마찬가지로 워크 카운트로 정지를 결정하고, reactor는 마찬가지로
interrupter로 깨웁니다. 차이는 모두 의도적이며, 하나도 빠짐없이 대장에
기록되어 있습니다.

전체 14장. 각 장은 고정된 6부분 형태: 구성 요소가 왜 필요한지, 데이터 구조와
많아야 세 개의 불변식, 한 연산의 전 생애 트레이스, 가장 중요한 코드 구간의
정독, asio와의 비교, 실험 하나와 버그 사냥 하나. 이 형태를 외울 필요는 없습니다
— 같은 사고 과정의 반복 훈련입니다.

## 전체의 경로

sasio의 구성 요소 의존 관계는 한 줄이고, 이 책은 그 줄을 따라갑니다:

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

앞의 두 장은 의존 체인에 있지 않지만, 없으면 체인 읽기가 흔들립니다: 제2장은
reactor/proactor 모델을 세우고 전체가 기대는 세 개의 엔진 계약을 제시합니다.
제12~14장은 asio로 돌아갑니다: 잘려나간 2계층(executor와 completion token)이
어떻게 작동하는지, 진짜 asio를 어떻게 읽을지.

모든 구성 요소에는 자체 테스트가 따라옵니다 — sasio의 9개 테스트 파일
(`tests/asio/`)은 `asio/src/tests/unit/`에서 이식되었고, 어서션과 주석은
가능한 한 원본 그대로입니다. 즉 sasio에서 검증한 행동은 asio의 행동이지,
모조품의 근사가 아닙니다.

## 첫 코드

완전한 sasio 프로그램입니다 — `examples/ex01_post.cpp`의 본문:

```cpp
sasio::io_context ctx;

ctx.post([] { std::puts("handler 1"); });
ctx.post([] { std::puts("handler 2"); });
ctx.post([] { std::puts("handler 3"); });

ctx.run();
```

`post`는 세 개의 handler를 큐에 넣고 반환합니다. 이 순간에는 하나도 실행되지
않았습니다 — post의 약속은 "줄세우기"일 뿐 "실행"이 아닙니다. `run()`이야말로
executor입니다: 큐에서 handler를 하나씩 꺼내 실행하고, 큐가 비고, 타이머가 없고,
등록된 descriptor가 없어지면 — 할 일이 없어지면 — 돌아갑니다. 프로그램은 세
줄을 인쇄하고 끝납니다.

세 가지 행동이 한 번 더 시선을 붙잡을 만합니다:

- `post`가 반환할 때 handler는 **하나도 실행되지 않았습니다**. 발기와 실행이
  분리되어 있습니다. 이 경계선이 전체를 관통합니다;
- `run()`이 돌아오는 전제는 **할 일이 아무것도 없는 것** — 빈 큐뿐 아니라,
  보류 중인 타이머, 등록된 descriptor까지 포함합니다(그 "할 일 없음" 판정의
  정확한 형태는 제4장에서);
- `run()`이 일을 다 해서 돌아오면 io_context는 stopped 상태에 들어가고, 다시
  돌리려면 `restart()`가 필요합니다. 이것은 asio의 행동이며, sasio가 그대로
  베꼈습니다.

몇 줄 안 되는 코드에 세 가지 요소이 전원 등장합니다: **큐**(handler가 어디에서
줄서는가), **work**(무엇이 루프를 계속 살려 두는가), **정지**(언제 끝인가).
이들이 전반부의 핵심 요소로, 제3장과 제4장이 하나씩 맡습니다.

## sasio와 asio의 규모 대조

| | sasio | asio 1.38.2 |
| --- | --- | --- |
| 헤더 | 18개 | 600개(`include/asio` 아래 `.hpp`) |
| 줄 수 | 4,207(실측; `include/sasio` + `src`의 `.hpp`/`.cpp`) | 169,063(`.ipp` 제외; 포함하면 약 19.7만) |
| I/O 백엔드 | 1(WSAPoll) | 6(epoll / kqueue / select / dev_poll / IOCP / io_uring) |
| executor / completion token | 없음 | 있음, API 전체에 걸쳐 있음 |
| 테스트 | 9개, asio 자체 테스트에서 이식 | 수백 개 |

이 표는 "작은 쪽이 큰 쪽보다 낫다"는 뜻이 아니라 **읽는 순서**입니다:
4200줄에는 건너뛸 수 있는 기계가 한 줄도 없고, 169,063줄의 대부분은 있습니다.
전자를 완전히 이해하면, 후자는 "아는 모델을 알아보는 것"과 "각 층의 기계가
왜 존재하는지 이해하는 것"이라는 두 문제로 분해되어, 난도가 급락합니다.

## 연습

**실험**: sasio를 처음부터 빌드하고 전체 테스트를 돌립니다. 저장소 루트에서
(Windows + MSVC):

```bat
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

9개의 테스트가 모두 통과하고, 약 10초 걸립니다. Ninja가 없으면 기본
제너레이터로도 됩니다 — `cmake --build build`가 자동으로 MSBuild를
호출합니다(그 경우 실행 파일은 `build\Debug\`에 생깁니다).

**버그 사냥**: `tests/asio/io_context.cpp`를 열고 `io_context_test`의 예외
구간을 찾으세요: `int`를 던지는 handler와 정상 카운트 handler가 번갈아
배치되어 있고, 바깥은 try/catch 루프로 `run()`을 다시 돌립니다. 실행하기 전에
세 가지를 예측하세요:

1. 첫 handler가 던진 후, 그 **뒤에 줄서 있던** 정상 handler들은 이번 패스에서
   여전히 실행되는가?
2. 예외가 `run()`을 탈출했을 때, `stopped()`는 true인가 false인가?
3. 루프는 왜 정확히 세 번 돌아야 `break`할 수 있는가?

예측을 적어 두세요. 제4장이 `run_one_impl`을 정독할 때 답을 맞추는 자리가
있습니다 — 그때쯤이면 여러분은 이미 행동에서 구현을 역산할 수 있게 되어
있을 것입니다.

---

모델 편은 다음 장부터입니다. 먼저 문제처럼 보이지 않는 문제 하나: "비동기"의
비동기는 대체 어디에 있는가? `recv`의 반환에 결과가 담겨 있던 그 끝은,
어떻게 옮겨졌는가?
