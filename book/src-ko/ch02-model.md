<!-- translated-from: book/src/ch02-model.md sha256:edc7a0b2d266 -->

# reactor와 proactor: 비동기 연산의 완전한 생애

가장 익숙한 코드부터:

```cpp
char buf[1024];
int n = recv(sock, buf, sizeof(buf), 0);
```

`recv`가 반환할 때, 결과는 이미 손안에 있습니다: 데이터를 받았거나, 상대가
닫았거나, 오류가 났거나. 함수의 반환은 마침표입니다. 이것이 동기입니다.

동기의 문제는 대기가 둘 이상이 되어야 드러납니다. TCP 서버를 쓴다고 하면,
같은 스레드에서 `accept`로 새 연결을 기다리는 동시에 연결된 socket에서 데이터를
기다려야 합니다 — `accept`가 먼저 막히면 이미 연결된 상대는 아무도 상대하지
않습니다. 반대도 마찬가지입니다. 마침표 둘, 실행 경로 하나, 하나는 반드시 막힙니다.

가장 직관적인 출구는 스레드를 늘리는 것: 연결당 스레드 하나, 각 스레드가 자기
`recv`를 독점하고 서로 막지 않습니다. 쓰기는 확실히 편하고, 실제로도 동작합니다.
대가는 연결 수가 늘어날 때 청구됩니다 — 스레드마다 스택 한벌, 커널 스케줄링
전환, 그리고 대다수 스레드가 99%의 시간을 그저 매달려 있는 것. 연결이 수천 단위가
되면, 서버보다 먼저 스레드 방식이 붕괴합니다.

다른 길은 "기다림"을 집중시키는 것: 모든 호출 지점이 제각각 막히는 대신,
**하나의 스레드가 수많은 descriptor를 지켜보고, 준비된 것부터 처리합니다**.
기다림이 개별 `recv` 안에 흩어지는 대신 하나의 시스템 콜에 모입니다. 이것이
reactor입니다.

세 번째 길은 더 나아가 "읽기"와 "쓰기"라는 동작 자체도 넘기는 것입니다. 발기
시점에는 "읽을 수 있으면 나를 불러 줘"라고 말할 뿐이고, 데이터는 커널이
버퍼로 옮기며, 프레임이 건네주는 것은 이미 결과입니다. 이것이 proactor입니다.

셋의 차이는 한 문장으로 압축됩니다: **동기는 스스로 하고 결과를 손에 넣고
계속합니다. reactor가 받는 것은 "이제 할 수 있다"입니다. proactor가 받는 것은
바로 "끝났다"입니다**. 이 장 이후의 모든 내용은 마지막 절을 코드 안에서
펼치는 것입니다.

## 두 얼굴

**reactor**: 커널은 "어느 descriptor가 준비되었는가"만 대답하고, `read`/`write`는
직접 해야 합니다. select, poll, epoll, kqueue — 그리고 Windows의 WSAPoll도 —
모두 이 가족입니다. 받는 것은 **준비 상태 알림**입니다.

**proactor**: I/O 자체를 커널이 수행합니다. 발기 시 버퍼를 제출하면 콜백이
받는 것은 전송된 바이트 수 그 자체입니다. Windows의 IOCP(I/O Completion Port)가
교과서적 구현입니다. 받는 것은 **완료 알림**입니다.

asio는 두 얼굴을 모두 가지고 있고, 플랫폼으로 전환합니다:

- **Windows**: IOCP를 씁니다, 진짜 proactor. 발기 시 OVERLAPPED 구조체와
  버퍼를 함께 커널에 넘기고, 완료 포트 큐에서 나오는 것은 이미 끝난 일입니다;
- **POSIX**: epoll/kqueue/select는 본질적으로 reactor이며, asio는 이것으로
  proactor를 **모방**합니다 — 준비 상태 관심을 등록하고, poll이 준비를 보고하면,
  프레임이 자신이 시스템 콜을 수행한 뒤 결과를 "완료"로 포장해 handler에게
  전달합니다. 모방의 핵심은 마지막 단계입니다: 사용자에게 보이는 것은 여전히
  "결과가 도착했다"이지 "이제 손대도 된다"가 아닙니다.

sasio는 Windows에서 굳이 IOCP를 쓰지 않고 모방 경로를 선택했으며, WSAPoll을
reactor로 씁니다. 이유는 실용적입니다: IOCP의 완료 포트, OVERLAPPED,
`GetQueuedCompletionStatus`는 모델을 온전한 Win32 기계에 묻어버리므로, 읽는
사람이 비동기를 보기 전에 먼저 Windows를 넘어야 합니다. WSAPoll 버전은 모든
줄이 socket 의미론 위에 머물고, asio의 POSIX 백엔드(`epoll_reactor` /
`select_reactor`)와 구조적으로 동형입니다 — 계층과 책임이 1:1로 대응하고,
코드량만 한 자릿수 차이입니다. 대가는 이 책이 진짜 proactor 하나를 다루지
않는다는 것 — 제14장의 읽기 지도에 `win_iocp_io_context` 보충 경로가
있습니다.

어느 얼굴이든, asio가 보여주는 API는 동일합니다: `async_read_some`은 두
플랫폼에서 같은 행동을 합니다. 이 "API는 불변, 얼굴은 변한다"는 사실은
거꾸로 말하면 **엔진 계약**이 플랫폼과 독립적으로 존재할 수 있음을
증명합니다. 다음 세 문장이 전체의 기초입니다.

## 엔진 계약(전체의 세 가지 불변식)

**계약 1: 발기 함수는 handler를 결코 인라인 실행하지 않습니다.** I/O 완료
(read/write/connect/accept/resolve)는 모두 `scheduler::post`로 큐에 들어갑니다;
만료된 타이머는 scheduler가 run 루프 안에서 직접 호출합니다(이것도 발기 함수
안에서는 아닙니다); `dispatch`는 "이 스레드가 이미 이 context를 처리 중일 때"
만 인라인으로 실행합니다 — 이것이 유일한 정당한 예외이며, 조건은 검증 가능
합니다. 발기 함수가 하는 일은 하나뿐입니다: "준비되면 무엇을 할지" 등록하는
것. (제10장의 `strand::dispatch`는 같은 원리가 직렬화 계층에서의 재현입니다:
판단 대상이 "이 context"에서 "이 strand"로 바뀌지만, 마찬가지로 호출 스레드가
`run()` 안에 있어야 합니다 — asio의 `strand_service::do_dispatch`에 있는
`can_dispatch() && !locked_`와 정확히 대응합니다.)

**계약 2: handler는 언제나 `run()` 안에서 실행됩니다.** 발기 함수가
반환하면 프로그램은 원래 경로를 계속합니다; 결과는 미래의 어느 시점에, `run()`을
호출한 스레드가 전달합니다. `run()`이 없으면 handler는 하나도 실행되지 않습니다
— 데이터가 오래전에 도착했더라도. sasio의 모든 제출 경로는 이것을 지키며,
strand도 예외가 아닙니다(제10장). 초심자가 가장 흔히 빠지는 함정이 여기
있습니다: post하고 `run()`을 잊으면, 코드는 조용히 아무것도 하지 않습니다.

**계약 3: 알림의 출처는 정확히 세 가지입니다.** handler 큐(`post`로 들어온 것),
준비된 descriptor(reactor가 보고한 것), 만료된 타이머(힙 꼭대기). 엔진 루프의
매 주기마다 세 출처를 모두 다시 확인하고, 일이 있는 것을 처리합니다.
멀티스레드 경합, 깨우기, 정지 규칙은 모두 이 세 출처를 중심으로 전개됩니다.

이 세 문장을 외워 두세요. 제4장의 워크 카운트, 제5장의 깨우기, 제6장의 취소는
모두 이 세 문장의 기초 위에 지어진 집입니다. 나중에 어떤 동작이 의외로
느껴지면, 계약으로 돌아가 다시 읽으면 대부분 스스로 도출할 수 있습니다.

## 한 `async_read_some`의 생애

가장 흔한 체인입니다. 단일 스레드, socket 연결됨, 상대는 언제든 보낼 수 있음:

```text
 user code                socket                  reactor                scheduler
    | async_read_some(buf,h)  |                       |                       |
    |------------------------->| store buf/h, register |                       |
    |                         | read interest         |                       |
    |                         |---------------------->| start_read(fd)        |
    |   returns (nothing happened yet)                 |                       |
    |<------------------------|                        |                       |
    |                         |                        |  WSAPoll(all fds)     |
    |                         |                        |== peer data arrives ==|
    |                         |  do_read(): recv()     |                       |
    |                         |<----------------------| invokes the registered |
    |                         |  got n bytes           | callback              |
    |                         |-------- post(h(n,ec)) --------->               | enqueue
    |                         |                        |                       |
    |     h(ec, n) runs inside the run() loop           |                       |
    |<--------------------------------------------------------- ------------|
```

이 체인에는 세 번의 인계가 있습니다 — 단일 스레드 프로그램이므로, 세 개의
스레드가 아니라 세 개의 시점입니다:

1. **발기**: `async_read_some`은 버퍼와 handler를 socket 객체에 저장하고,
   reactor에 이 descriptor의 읽기 준비 관심을 등록한 뒤 반환합니다. 데이터는
   전혀 건드리지 않았습니다 — 이 시점에서 상대는 아직 아무것도 보내지
   않았을 수 있습니다;
2. **준비**: `run()` 루프가 `WSAPoll`에서 잠듭니다. 상대의 데이터가 도착하고,
   poll이 반환하면, reactor가 이 descriptor에 등록된 콜백을 찾아 호출합니다 —
   그 콜백은 여러분의 handler가 아니라 socket 자신의 `do_read`입니다;
3. **전달**: `do_read`가 진짜 `recv`를 수행하고, 바이트 수를 얻어, 여러분의
   handler를 결과와 함께 scheduler 큐에 `post`합니다. run 루프의 다음 주기가
   꺼내어 실행합니다.

2단계와 3단계의 분업에 주목하세요: **reactor는 "깨우기"만 담당하고, 진짜 I/O를
하는 것은 socket의 `do_read`입니다**. 이것이 "reactor로 proactor를 흉내 내는"
다리 — 깨운 다음 시스템 콜을 보태면, 사용자가 받는 것은 여전히 완료 알림입니다.
계약 1은 이 체인에서 두 번 검증됩니다: 발기 시 handler를 실행하지 않고,
`do_read` 이후에도 실행하지 않습니다(오직 post) — 여러분의 handler는 오직
4단계에만 등장합니다.

멀티스레드 `run()`은 이 체인에 경합을 도입합니다: 두 스레드가 모두 `WSAPoll`에서
잠들어 있을 때, 누구를 깨우고, 준비 상태가 두 번 전달되지 않는가 — 이것이
제5장의 주제이며, 이 책에서 가장 인상 깊은 버그 광맥이기도 합니다.

## 정독: 엔진 루프의 골격

이 체인은 모두 같은 루프로 합류합니다. `src/scheduler.cpp`의 `run_one_impl`
(83줄)이 라이브러리에서 가장 중요한 코드 구간입니다. 먼저 골격만 — 잠금과
카운트를 벗겨낸 형태:

```cpp
for (;;)
{
  if (stopped())                        // someone called stop(): quit now
    return false;

  if (!queue_.empty())                  // source 1: the handler queue
    { pop one and invoke it; return true; }

  if (earliest timer expired)           // source 2: the timer heap
    { pop and invoke its handler; return true; }

  timeout = next timer expiry;          // source 3 decides how long we may sleep
  reactor_.run_for(timeout);            // sleep in WSAPoll until any source fires
}
```

세 줄이 세 계약의 착지점입니다: 각 `return true` 이전의 실행이 계약 2(모든 것이
run 루프 안); 소스 1과 소스 2의 분기 — 큐의 handler는 "누군가 post한 것"이고,
타이머의 handler는 scheduler가 직접 호출하는 것 — 이 바로 계약 1의 "큐를
경유하지 않음"입니다; 마지막 두 줄은 계약 3의 세 번째 출처로, 유일하게 막히는
곳입니다.

하나의 세부사항이 단락 하나를 받을 만합니다: `run_for`가 반환한 후, 제어 흐름은
루프 꼭대기로 돌아가서 **큐를 다시 봅니다**. 왜일까요? `WSAPoll`에서 잠든 그
시간 동안, 어떤 스레드든 새 handler를 `post`했을 수 있기 때문입니다 — 돌아가서
보지 않으면 놓칩니다. 엔진 전체의 형태가 바로 이 한 문장: **매 주기마다 세 출처를
모두 다시 검사한다**. 제5장에서는 "다른 스레드가 post한 뒤 잠든 스레드가 깨워진다"
를 위해 asio와 sasio가 각각 무엇을 지불했는지 다룹니다. 지금은 이 형태만
기억하세요.

골격에서 지워진 세 가지 세부 — work 카운트가 "할 일이 남았는가"를 어떻게
계산하는지, `stopped()` 이후 실행되지 않은 handler는 어디로 가는지, 잠금을
어떤 순서로 잡아야 교착하지 않는지 — 모두 제4장에서 전문을 정독하며 채웁니다.

## 대조표

| 질문 | asio (Windows 얼굴) | asio (POSIX 얼굴) | sasio |
| --- | --- | --- | --- |
| 대기 기제 | IOCP 완료 포트 | epoll / kqueue / select | WSAPoll |
| I/O를 수행하는 주체 | 커널(진짜 proactor) | 준비 후 프레임이 직접 | 준비 후 프레임이 직접 |
| 소스 입구 | `detail/win_iocp_io_context.hpp` | `detail/epoll_reactor.hpp`, `detail/select_reactor.hpp` | `src/reactor.cpp` |
| 타이머 구동 | 완료 포트 대기 시간 초과 | reactor가 poll 타임아웃 계산 | scheduler가 poll 타임아웃 계산 |
| 대응 관계 | — | sasio와 동형 | — |

한 문장: **sasio는 asio의 POSIX 경로를 택해 Windows 위에서 구동했습니다**.
sasio의 reactor를 읽는 것은 asio의 `epoll_reactor` 축소판을 읽는 것입니다.
읽은 뒤 POSIX 얼굴의 asio 소스로 가도, 모델의 머리를 바꿀 필요가 없습니다.
진짜 proactor(IOCP 얼굴)의 차이는 제14장으로 미룹니다.

## 연습

**실험**: echo 서버를 빌드하고 실행합니다:

```bat
cmake --build build --target ex03_echo_server
build\ex03_echo_server.exe 5555
```

터미널을 두 개 더 열어 각각 `telnet 127.0.0.1 5555`로 접속하고 번갈아 문자를
입력하면, 양쪽 모두 에코를 받습니다. 단일 `run()` 스레드가 동시에 두 연결을
처리하고 있는 것 — 이 장의 시퀀스 다이어그램으로 설명하세요: 두 socket이 같은
descriptor 테이블에 등록되어 있고, 한 번의 `WSAPoll`이 양쪽의 준비를 보고하며,
준비된 것부터 처리합니다. 한 클라이언트를 연결만 하고 아무것도 보내지 않게 해
보세요: 다른 쪽의 에코는 영향을 받지 않습니다. 그 descriptor는 준비되지
않았으므로, 루프가 그 위에서 시스템 콜을 낭비하지 않기 때문입니다.

**버그 사냥**: `tests/asio/steady_timer.cpp`에서 cancel 관련 케이스를
찾으세요: `expires_after` + `async_wait` 후에 `cancel()`을 호출합니다. 반환값은
1(하나 취소)이지만 handler는 **그래도 호출됩니다** — `operation_aborted`
오류를 받아서. 먼저 두 가지를 예측하세요: handler는 어느 시점에 호출되는가?
`cancel()`의 호출 스택 안에서 동기적으로 호출된다면 어떤 문제가 생기는가? 그런
다음 실행하여 확인합니다. "취소는 통보가 없다는 뜻이 아니다"가 제6장의 핵심
계약이고, 그 이유(재입 방지)는 이 시점에서 이미 짐작할 수 있습니다.

---

모델이 섰습니다: 발기는 등록만 하고, 완료는 `run()` 안에서 이루어지며, 알림은
세 출처에서 옵니다. 그런데 "발기는 등록만 한다"에는 전제가 있습니다 — 등록할
곳이 있어야 합니다. socket의 상태는 어디에 저장되며, reactor와 타이머 힙은 누가
소유하고, `io_context` 자신은 거의 로직이 없는 이유는 무엇일까요? 다음 장:
서비스 레지스트리.
