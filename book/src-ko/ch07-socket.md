<!-- translated-from: book/src/ch07-socket.md sha256:bd7c0e04fd3b -->

# socket: 발기 함수는 "준비되면 할 일"만 기술한다

엔진의 3장이 도구를 모두 갖추었습니다: 큐가 순서를 관리하고, reactor가 준비
상태를 관리하고, 타이머 힙이 시간을 관리합니다. 이 장부터 엔진을 실제로
사용합니다. 먼저 `src/ip_tcp.cpp`의 실제 코드, `async_read_some`의 발기
함수를 보십시오:

```cpp
void socket::start_read_some(mutable_buffer buffer,
    std::function<void(error_code, std::size_t)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor, 0);
    });
    return;
  }

  if (read_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started, 0);
    });
    return;
  }

  read_pending_ = true;
  read_buffer_ = buffer;
  read_handler_ = std::move(handler);
  reactor_.start_read(handle_, [this]() { do_read(); });
}
```

세 번 읽어도, `recv`는 찾을 수 없습니다. 이 함수가 한 것은 정확히 네 가지
입니다: 상태를 검사하고, 버퍼와 handler를 **멤버에 저장**하고, "읽을 수 있으면
나를 호출하라"는 관심을 하나 등록하고, 반환합니다. 진짜 `recv`는 다른 함수
`do_read` 안에 있으며, reactor가 준비 상태를 보고할 때만 호출됩니다.

이 장은 "발기"와 "완료"의 2상을 모두 엽니다 — 이것이 asio의 모든 socket 코드의
공통 형태이며, `basic_stream_socket`의 모든 `async_*`가 이 형태를 하고 있습니다.

## 설계: 방향당 하나의 슬롯

socket의 상태는 많지 않지만, 모든 멤버가 각각의 이유가 있습니다:

```cpp
SOCKET handle_ = INVALID_SOCKET;

bool read_pending_ = false;
std::function<void(error_code, std::size_t)> read_handler_;
mutable_buffer read_buffer_;
// a symmetric set for the write direction
```

불변식 3개:

1. **방향당 최대 하나의 미완료 연산**. `read_pending_`이 슬롯 점유 표시입니다
   — 두 번째 읽기 발기는 `post`로 전달되는 `already_started`를 받습니다
   (주: 여전히 post이며, 오류 경로에서도 계약 1은 깨지지 않습니다). asio는
   operation 큐로 임의 수를 지원하지만, sasio는 1 슬롯입니다 — 용량의 차이가
   아니라 형태의 차이입니다;
2. **read/write/accept의 발기 함수는 데이터면 시스템 콜을 수행하지 않습니다**;
   connect가 예외입니다 — 발기 시 1회의 비블로킹 connect 시도를 합니다
   (connect에는 "준비"라는 개념이 없습니다: 즉시 성공하거나, 즉시 실패하거나,
   진행 중이며, 세 번째 경우만 쓰기 준비 관심을 등록합니다);
3. **완료의 전달은 모두 `post`를 거칩니다**, 즉시 실패도 포함하여 —
   `bad_descriptor`는 그 자리에서 던져지는 것이 아니라 큐에 들어가 전달되어,
   handler가 항상 `run()` 안에서 실행됨을 보장합니다(계약 2).

또 하나 반드시 명명해야 할 경계가 있습니다: **버퍼와 handler의 생존 기간은
발기 측의 책임입니다**. `read_buffer_`가 저장하는 것은 포인터와 길이이지
데이터의 복사본이 아닙니다 — 보류 중인 동안 호출자는 버퍼를 살아 있게
유지해야 합니다. asio도 같습니다. 이것은 C++ 비동기 I/O의 표준 관례입니다.

## 한 읽기 연산의 2상식

제2장의 시퀀스 다이어그램을 코드에 적용합니다. **제1상(발기)**은 위의
`start_read_some`이며, `reactor_.start_read(handle_, [this]() { do_read(); })`
로 끝납니다.

**제2상(준비)**은 reactor가 `WSAPoll`이 반환한 후에 호출합니다:

```cpp
void socket::do_read()
{
  if (!read_pending_)
    return;

  error_code ec;
  const int result = detail::socket_ops::recv_socket(handle_,
      read_buffer_.data(), clamp_to_int(read_buffer_.size()), ec);

  if (result < 0)
  {
    if (ec == would_block)
    {
      // Spurious readiness: register again and wait.
      reactor_.start_read(handle_, [this]() { do_read(); });
      return;
    }

    complete_read(ec, 0);
    return;
  }

  complete_read(error_code(), static_cast<std::size_t>(result));
}
```

여기가 `recv`의 실제 구현 위치입니다. 세 가지 출구:

- **거짓 경보**: poll은 읽을 수 있다고 했지만, `recv`가 `would_block`을
  반환합니다(검사와 호출 사이의 창에서 데이터가 사라졌거나, poll의 레벨
  트리거 의미론 자체가 약간의 모호함을 가지고 있습니다). 대응은 다시 등록하고
  대기로 돌아가는 것 — 제5장 불변식 2("꺼내서 실행")가 명시적 재등록을
  필수로 만듭니다. 그렇지 않으면 이 읽기 연산은 reactor의 세계에서 사라집니다;
- **진짜 오류**: `complete_read(ec, 0)` — 점유 표시를 클리어, 관심을 등록
  해제, handler를 오류 코드와 함께 `post`합니다;
- **성공**: `complete_read(빈 오류 코드, n)`, 같은 경로입니다.

`complete_read` 자체는 십여 줄입니다: `read_pending_`을 클리어, reactor의
관심을 등록 해제, handler를 멤버에서 move로 끌어내고, `post`합니다. 슬롯이
비어지는 것은 **슬롯이 다음 발기로 즉시 재사용될 수 있음**을 의미합니다 —
이전 handler가 아직 큐에서 실행을 기다리고 있어도, 새로운 `async_read_some`은
이미 합법입니다. handler의 생애와 슬롯의 생애가 여기서 분리됩니다.

## 2개의 비대칭: connect와 close

**connect에는 동기의 끝이 없으며, 3 상태가 있습니다**. 발기 함수
`start_connect`는 비블로킹 `connect`를 직접 호출합니다:

- 즉시 성공(루프백에서 흔함): `complete_connect(빈 오류 코드)`;
- 즉시 실패(도달 불가 등): `complete_connect(ec)`;
- `would_block`(연결 진행 중): 쓰기 준비 관심을 등록하고, 커널이 3방향
  핸드셰이크를 끝내기를 기다립니다.

세 번째 준비 후, `do_connect`는 데이터를 쓰지 않습니다. 하는 일은 하나 —
`getsockopt(SO_ERROR)`로 연결의 최종 결과를 읽고, `complete_connect`에
넘깁니다. **여기서 "쓰기 가능"의 의미는 "핸드셰이크가 끝났거나 실패했으며",
결과는 SO_ERROR에서 수령합니다**. 이것이 비블로킹 connect의 표준 작법이며,
asio의 `detail/reactive_socket_connect_op`도 같은 흐름입니다.

**close는 완료를 주입하며, 철회하는 것이 아닙니다**. `socket::close()`는 먼저
`cancel()`: 보류 중인 각 방향에 `complete_xxx(operation_aborted)`를 호출합니다
— handler가 `post`되어 "취소됨" 오류 코드를 받습니다. 그 후에야
descriptor의 등록을 해제하고 핸들을 닫습니다. 따라서 close 후에도 보류 중인
**모든** 연산의 완료 통보를 반드시 받게 되며, 오류 코드가
`operation_aborted`일 뿐입니다. 제6장 타이머의 "취소는 통보가 없다는 뜻이
아니다"가 socket에서 그대로 재현되며, 이유도 같습니다: `close()`를 호출하는
스택이, handler가 건드리려는 스택 그 자체일 수 있으며, 그 자리에서 호출하는
것은 재입입니다.

acceptor는 socket의 특수형입니다: `async_accept`는 읽기 준비를 등록합니다
(새 연결의 도착 = 리스닝 socket이 읽을 수 있음), `do_accept`에서 `accept()`가
새 핸들을 받고, 비블로킹으로 설정하며, `peer->assign()`으로 호출자가 제공한
socket 객체에 인도합니다. 읽기와 완전히 동형이며, 별도로 전개하지 않습니다.

## 정독: cancel과 complete_read

"취소"의 2개 코드를 연결하여 읽습니다 — socket 생애의 마무리 동작입니다:

```cpp
void socket::cancel()
{
  if (!is_open())
    return;

  if (connect_pending_)
    complete_connect(operation_aborted);
  if (read_pending_)
    complete_read(operation_aborted, 0);
  if (write_pending_)
    complete_write(operation_aborted, 0);
}

void socket::complete_read(error_code ec, std::size_t bytes)
{
  read_pending_ = false;
  reactor_.stop_read(handle_);

  std::function<void(error_code, std::size_t)> handler =
      std::move(read_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec, bytes]() mutable {
    handler(ec, bytes);
  });
}
```

3개의 세부사항:

- `complete_xxx`는 멱등한 마무리 게이트입니다: 플래그 클리어, 등록 해제,
  **handler를 멤버에서 move로 끌어내고**, post. 이 후에는 socket의 멤버에 이
  연산의 흔적이 남지 않아, 소멸도, close도, 다음 발기도 안전합니다;
- post의 lambda는 **handler와 결과를 값으로 캡처**합니다 — socket의 어떤
  멤버도 참조하지 않습니다. 이것이 close가 complete 직후에 핸들을 닫을 수
  있는 이유입니다. 던져진 통보는 자기 완결적입니다;
- `if (!handler) return;`는 "빈 슬롯이 취소된" 퇴화 케이스의 방어입니다 —
  정상 경로에서는 여기에 도달하지 않습니다.

asio와의 대조: `basic_socket::cancel`은 `reactor.cancel_ops(descriptor)`를
거쳐, reactor가 그 descriptor의 operation 큐 전체를 하나씩
`operation_aborted`로 하여 던집니다. 기제는 다릅니다(큐 vs 슬롯), 행동은
같습니다(완료는 반드시 도달하며, 결코 재입하지 않습니다).

## 대조표

| 질문 | asio | sasio |
| --- | --- | --- |
| 발기 함수 | `basic_stream_socket.hpp`의 `async_read_some` | `src/ip_tcp.cpp`의 `start_read_some` |
| 완료의 담체 | `detail/reactive_socket_recv_op.hpp`(operation 객체: handler/할당자/work 단위) | 멤버 슬롯 + handler를 캡처한 `post` lambda |
| 보류 수 | 방향당 1 큐 | 방향당 1 슬롯 |
| connect | `reactive_socket_connect_op` + SO_ERROR | `start_connect` / `do_connect`, 같은 흐름 |
| 취소 | `reactor.cancel_ops`, op마다 던짐 | `cancel()`, 슬롯마다 던짐 |
| 핸들 계층 | `detail/socket_ops.hpp` + `impl/socket_ops.ipp`(5,140줄) | `src/socket_ops.cpp` + 헤더(259줄) |

asio의 operation 객체에 한 문장 더: 그것은 "이벤트가 일어날 때까지 살아 있는
상태 운반체"로, handler, 할당자, work 단위, 완료 결과를 하나의 힙 위의 객체에
묶어 있습니다. sasio는 멤버 슬롯으로 대체했습니다 — 대가는 방향당 하나의 보류
(불변식 1), 이익은 이 장이 2상식 자체에 집중할 수 있는 것. 제8장의 합성
연산은 "체인의 끝까지 살아 있는 상태"를 또 필요로 하며, 거기서 shared_ptr
릴레이를 보게 됩니다.

## 연습

**실험**: ch2의 echo 서버를 socket 수준으로 내립니다.
`examples/ex03_echo_server.cpp`의 `session::do_read` / `do_write_some`을
기반으로, 답하고 검증하세요: 2개의 연결이 동시에 1KB씩 보낼 때, 단일
스레드 `run()` 안에서 이 2개의 session의 `do_read`는 교차 실행됩니까? 두
handler에 표시와 타임스탬프를 인쇄하여 검증하세요. (예상: 교차합니다 — 각
session의 `async_read_some` 완료는 독립된 큐 항목이며, 준비된 것부터
처리됩니다.)

**버그 사냥**: `socket::close()` 후, 투척된 `operation_aborted` handler가
실행되기 전에, 사용자가 즉시 같은 socket 객체에 새로운 `async_read_some`을
발기했습니다. `handle_`은 이미 `INVALID_SOCKET`입니다 — `start_read_some`은
어느 분기를 통과합니까? handler는 어떤 오류 코드를 받습니까? 두 통보(오래된
aborted, 새로운 bad_descriptor)의 순서가 보장됩니까? 코드에서 답을 도출한 후,
20줄 프로그램으로 검증하세요.

---

한 번의 `async_read_some`이 돌려주는 것은, 대개 일부입니다 — TCP는 바이트
스트림이고, 한 번의 `recv`가 얼마를 줄지는 커널 마음대로입니다. "가득 찰 때까지
read_some을 반복하는" 것을, 자체적으로 릴레이하는 재사용 가능한 연산으로 만든
것이 다음 장의 합성 연산 `async_read`입니다.
