<!-- translated-from: book/src/ch09-udp-resolver.md sha256:16a0cc61db85 -->

# UDP와 resolver: 하나의 엔진, 두 가지 재활용

엔진의 3대 부품이 자리를 잡았고 TCP의 2상식이 세워졌으므로, 사용 가능한
네트워크 라이브러리의 골격은 이미 완성되었습니다. 이 장은 일상적인 마지막 2개의
컴포넌트를 맞춥니다. 각각이 고유한 교훈을 가지고 있습니다:

**UDP**는 시스템 콜을 교체합니다(`sendto` / `recvfrom`)만, **엔진에는 거의
손을 대지 않습니다** — 같은 descriptor 테이블, 같은 `WSAPoll`, 같은 "등록,
준비, 실행, post". 읽고 나면 확인할 수 있습니다: 제5장의 reactor는 태어날 때부터
프로토콜에 얽매이지 않는 구조였습니다.

**resolver**(이름 해결)는 정반대입니다: `getaddrinfo`는 블로킹 호출이며,
descriptor도 없고 준비 상태도 없어서, reactor의 모델로는 처리할 수 없습니다.
asio의 답은 스레드 풀입니다. sasio의 답은 **질의당 하나의 짧은 수명 스레드**입니다.
라이브러리에서 유일하게 스레드를 도입하는 컴포넌트이자, "work 단위가 `run()`을
유지한다"의 최고의 교재입니다.

## UDP: 시스템 콜만 다를 뿐

`include/sasio/ip/udp.hpp`의 socket은 TCP 버전과 필드별로 대조됩니다:

```cpp
bool send_pending_ = false;
std::function<void(error_code, std::size_t)> send_handler_;
const_buffer send_buffer_;
endpoint send_destination_;              // the extra one: where to

bool receive_pending_ = false;
std::function<void(error_code, std::size_t)> receive_handler_;
mutable_buffer receive_buffer_;
endpoint* receive_sender_ = nullptr;     // the extra one: write back who sent it
```

비동기 입구는 정확히 4개입니다: `async_send_to(buffer, destination, handler)`와
`async_receive_from(buffer, sender, handler)`(연결 없는 프로토콜에는 "연결"이라는
개념이 없고, 송수신마다 상대의 주소가 붙습니다. 그래서 connect/accept도 없고,
슬롯의 조합은 3세트에서 2세트로 줄어듭니다).

2상식의 형태는 TCP와 완전히 동형입니다. 수신을 예로:

- **발기** `start_receive_from`: 버퍼를 저장, `sender` 포인터를 저장, 읽기 준비를
  등록, 반환합니다;
- **준비** `do_receive`: 한 번의 `recvfrom`으로 완전한 데이터그램을 받아들이고,
  송신자 주소를 `from_sockaddr`을 통해 `*receive_sender_`에 써넣은 후,
  `complete_receive`에 넘깁니다;
- **전달**: handler는 `post`로 나가며, 바이트 수와 송신자 엔드포인트를 가지고
  있습니다.

오류 경로도 TCP를 그대로 복사합니다: 가짜 경보에서는 재등록, 진짜 오류에서는
`complete_receive(ec, 0)`, `close()`는 보류 방향에 먼저
`complete(operation_aborted)`를 호출합니다. 제7장의 모든 결론이 UDP에서 그대로
성립하며, 예외는 하나도 없습니다 — 이것이 바로 reactor 추상화의 가치입니다:
**프로토콜이 바뀌어도, 엔진은 바뀌지 않습니다**.

UDP 특유의 주의점이 2개:

- **`receive_sender_`는 raw 포인터입니다**. 보류 중에는 유효한 `endpoint`를
  가리켜야 합니다 — "버퍼는 살아 있어야 한다"와 같은 사용자 책임입니다.
  데이터그램이 도착했을 때 써넣는 것은 "이 데이터그램이 어디에서 왔는가"이지
  "이 socket이 누구와 연결되어 있는가"가 아닙니다(UDP에는 연결이라는 개념이
  없습니다);
- **데이터그램에는 경계가 있습니다**. 한 번의 `recvfrom`이 하나의 완전한
  데이터그램을 받아들입니다. 버퍼가 너무 작으면 잘립니다(asio의 행동은
  `socket_ops.ipp`의 `WSAEMSGSIZE` 처리를 통해 데이터그램을 오류로 하여
  버립니다. sasio는 오류를 handler에 전달합니다 — 부록 E 참조). TCP의
  "N바이트를 정확히 읽기" 로직은 여기서는 적용되지 않습니다 — 스트림이
  없으므로, 합성 연산의 출처도 없습니다.

## resolver: 블로킹 호출의 비동기화

`getaddrinfo("example.com", "80")`은 수백 밀리초에서 수초가 걸릴 수 있으며
(DNS를 조회하러 가므로), 그 동안 스레드 전체가 멈춥니다. run 루프에 넣으면
모든 handler가 발이 묶이고, reactor에 넣어도 해결되지 않습니다 — descriptor가
없습니다. 유일한 출구: **다른 스레드에 던지고, 자신은 계속 달린다**.

sasio의 `resolver::start_resolve` 전문(라이브러리에서 유일한 스레드 코드입니다.
통째로 읽을 가치가 있습니다):

```cpp
void resolver::start_resolve(const query& q,
    std::function<void(error_code, results)> handler)
{
  auto cancelled = std::make_shared<std::atomic<bool>>(false);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancel_flag_ = cancelled;               // cancel() touches the latest query only
  }

  scheduler* sched = &scheduler_;
  scheduler_.on_work_started();             // ← work unit #1

  try
  {
    std::thread([sched, q, cancelled, handler = std::move(handler)]() mutable {
      error_code ec;
      results resolved = do_resolve(q, ec); // blocking getaddrinfo happens here

      // Decide cancellation once, when the lookup has finished. A cancel()
      // that arrives after this point no longer changes the result, the same
      // way asio leaves an already-completed operation untouched.
      const bool aborted = cancelled->load(std::memory_order_acquire);

      sched->post(
          [sched, handler = std::move(handler), ec,
              resolved = std::move(resolved), aborted]() mutable {
            struct release
            {
              scheduler* target;
              ~release() { target->on_work_finished(); }   // ← the unit returns
            } guard = {sched};

            handler(aborted ? operation_aborted : ec, resolved);
          });
    }).detach();
  }
  catch (...)
  {
    scheduler_.on_work_finished();          // no thread, the unit returns now
    throw;
  }
}
```

5가지 포인트, 시간 순서로:

1. **work 단위는 스레드보다 먼저 성립합니다**. `on_work_started()`가
   `std::thread` 생성자의 **전**에 호출됩니다 — 이 순간부터, 4조건의
   `work_count_ != 0`이 성립하고, `run()`은 "큐가 비고, 타이머가 없고,
   descriptor가 없다"로 멈추지 않습니다. 이것이 없으면, 스레드는 아직 해결
   중인데 run 루프가 빠져나가 context가 파괴되고, 돌아온 `post`는 허공을
   가리키는 포인터를 역참조하게 됩니다;
2. **스레드는 detach입니다**. 캡처하는 것은 값만(scheduler 포인터, query, 플래그,
   handler)이고, join할 사람이 필요 없습니다. 대가는 하나의 단단한 제약:
   **io_context는 미완료의 `async_resolve`보다 오래 살아 있어야 합니다** —
   스레드 안의 `sched->post(...)`가 역참조하는 scheduler 포인터는, context가
   파괴된 후에는 허공을 가리킵니다;
3. **취소는 회수가 아니라 샘플링입니다**. `cancel()`은 최근 질의의 플래그를
   true로 뒤집을 뿐입니다. 스레드 내의 `getaddrinfo`는 중단될 수 없으며, 스스로
   반환하기를 기다려야 합니다. `aborted`는 **스레드 안에서, 검색이 끝난 후**에
   한 번만 샘플됩니다 — 이 순간 이후의 `cancel()`은 결과를 바꾸지 않습니다.
   이것은 asio의 "이미 완료된 연산은 취소의 영향을 받지 않는다"와 일치합니다.
   샘플링 위치 *가* 의미론입니다;
4. **결과는 `post`로 run 루프에 돌아옵니다**. 스레드가 handler를 직접 호출하는
   것은 절대로 없습니다 — 스레드가 가진 결과(오류 코드 + 엔드포인트 리스트)는
   값으로 lambda에 캡처되어, 큐에 던져집니다;
5. **work는 post된 lambda 위에서 돌아옵니다**. `release` 가드의 소멸자는
   handler가 실행된 **후**에 발화합니다 — 해결 중에도(handler가 큐에서 대기하는
   동안도 포함하여) work 단위는 카운트에 남아 있고, handler가 실행을 마친 후에야
   해방됩니다. 예외에서도 누출되지 않습니다. RAII의 고전 기법으로, 제4장
   `handler_work`의 크로스스레드 사촌입니다.

동기판 `resolve()`는 같은 `do_resolve`를 공유하며, 실패 시
`std::system_error`를 던집니다(오류 코드는 독립된 resolver 카테고리에 실려 있고,
메시지는 `gai_strerror`에서 옵니다). 교육의 편의를 위한 것으로, 실제 코드는
비동기판을 사용하십시오.

## 대조표

| 질문 | asio | sasio |
| --- | --- | --- |
| 해결의 executor | 상주 스레드 풀(`detail/resolver_thread_pool`) | 질의당 하나의 짧은 수명 detach 스레드 |
| 취소 | 완전한 취소 큐, 질의별 | 샘플링 플래그, 최근 질의만 |
| 결과 | `results`(반복 가능, 서비스 힌트로 정렬됨) | `std::vector<endpoint>`(IPv4) |
| 스레드 수명 | 풀은 context와 운명을 같이함 | detach, work 단위 + 사용자 보증에 의존 |
| endpoint 타입 | `udp::endpoint` / `tcp::endpoint`가 별개 | 하나의 `endpoint`를 재사용 |
| datagram socket | `basic_datagram_socket.hpp` | `src/ip_udp.cpp` |

`udp::endpoint` 재사용에 독립적인 주를 하나: asio에서는 2개의 별개 타입입니다
(둘 다 IPv4 주소/포트 쌍임에도 불구하고), "TCP 엔드포인트를 UDP socket에
보내는" 실수를 컴파일 시에 잡습니다. sasio는 하나로 합쳤습니다 — 타입이
줄었지만, 검사가 실행 시간으로 연기됩니다. "교육판이 타입 체조를 자른다"의
또 하나의 사례입니다. 잘라낸 것에는 이름이 붙어 있고, 차이표에 등록되어
있습니다.

"질의당 스레드"도 같은 종류의 트레이드입니다. 질의가 많을 때는 스레드 생성
오버헤드와 취소할 수 없는 `getaddrinfo`가 실제 비용이며, asio의 상주 풀 +
취소 큐가 생산 수준의 답입니다. sasio가 주는 것은 **최소한으로 이해할 수
있는** 버전입니다 — 스레드, work, post의 3개 개념이 각각 한 번씩만 나타나서,
한 번 읽으면 투명하게 보입니다.

## 연습

**실험**: UDP echo 서버를 30줄 이내로 작성합니다:

```cpp
sasio::io_context ctx;
sasio::ip::udp::socket sock(ctx);
sock.open();
sock.bind(sasio::ip::udp::endpoint(5555));        // same port as ex03

char recv_data[1500];                              // one buffer for receiving
sasio::ip::udp::endpoint sender;

std::function<void()> do_receive = [&]() {
  sock.async_receive_from(sasio::buffer(recv_data), sender,
      [&](sasio::error_code ec, std::size_t n) {
        if (!ec)
        {
          auto payload = std::make_shared<std::vector<char>>(
              recv_data, recv_data + n);           // each send gets its own copy
          sock.async_send_to(sasio::buffer(*payload), sender,
              [payload](sasio::error_code, std::size_t) {});
        }
        do_receive();                              // keep receiving
      });
};
do_receive();
ctx.run();
```

`nc -u 127.0.0.1 5555`로 몇 줄을 두드려 에코를 확인합니다. 그 `payload`에
주목: 수신과 송신에 하나의 버퍼를 쓰는 것은 부족합니다 — `async_send_to`가
아직 끝나지 않은 동안에, 다음 `async_receive_from`이 이미 등록되어 있어서,
두 번째 데이터그램이 전송 중인 내용을 덮어씁니다. 송신마다 자신의 복사본을
만들고, lambda가 `shared_ptr`로 생존을 유지하며, 송신이 완료되면(handler의
파괴) 해방됩니다. 이 기법은 제8장과 같은 계보입니다. 제8장 `write_op`의 릴레이 배턴,
포켓판입니다.

**버그 사냥**: 2개의 타이밍 — `aborted`는 각각 무엇이 됩니까?

- 타이밍 A: `async_resolve` 발기 → 스레드가 아직 `getaddrinfo` 안에 있음 →
  `cancel()` → 검색 완료 → 샘플링;
- 타이밍 B: `async_resolve` 발기 → 검색 완료 → **샘플링 후** → `cancel()` →
  handler 실행.

`start_resolve` 코드에 대해 줄마다 확인하세요: A는 `operation_aborted`를
전달하고, B는 해결 결과를 전달합니다. 한 걸음 더: 샘플링이 post 후의 handler
안에서(스레드가 아니라) 행해졌다면, 타이밍 A의 취소 통보는 얼마나 늦어질까요?
— "샘플링 위치가 의미론"이라는 결론이, 코드 안에서 스스로 자라날 때까지
합니다.

---

TCP, UDP, 이름 해결, 합성 연산 — 사용 가능한 네트워크 라이브러리의 API 면이
완성되었습니다. 하지만 멀티스레드 `run()` 아래에서, 2개의 handler가 같은
`session`을 동시에 만지는 장면은 조만간 반드시 옵니다. 다음 장의 strand:
하나의 큐, 하나의 running 플래그가, "비동시"를 조합 가능한 보증으로 바꿉니다.
