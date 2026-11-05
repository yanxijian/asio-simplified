<!-- translated-from: book/src/ch10-strand.md sha256:c98c21cf9bfd -->

# strand: 하나의 큐와 하나의 running 플래그

제2장에서 말했습니다: 멀티스레드가 동시에 `run()`을 호출하면, 서로 다른
handler가 동시에 실행됩니다. 제4장과 제5장은 이 동시성의 부품을 모두
제공했습니다 — 두 스레드가 각각 `WSAPoll`에서 잠들고, 각각 큐에서 꺼내고, 각각
실행합니다. 여기서 비용이 드러납니다: echo 서버에서, 같은 session의 `do_read`와
`do_write_some`이 **동시에** 두 스레드 위에서 달리는 장면이 머지않아 반드시
오며, `write_offset_` 같은 평범한 멤버는 즉시 데이터 경쟁의 표적이 됩니다.

asio의 대답을 strand라고 부릅니다: **일련의 handler에 "결코 동시하지 않는다"는
표시를 합니다**. sasio의 `strand`는 `io_context`에 결합된 작은 객체이며,
`include/sasio/strand.hpp`와 `src/strand.cpp`의 합계는 272줄입니다 — 이 장이
그것을 엽니다. 그리고 방금 일어난 버그도 강의합니다: 이 저장소의 검토
프로세스가 집필 기간 중에 잡은 strand 의미론의 편차가, 제5장의 두 번의 깨움
누락과 나란히 커밋 히스토리에 누워 있습니다.

## 설계: 큐, 플래그, 배수 루프

strand의 내부 상태는 세 가지뿐입니다:

```cpp
scheduler& scheduler;                     // the drain borrows the scheduler
std::mutex mutex;
std::deque<std::function<void()>> queue;  // queued handlers
bool running = false;                     // is a drain loop running
```

불변식 2개:

1. **동일 strand의 handler는 병행하지 않으며, `post` 순서가 실행 순서입니다**.
   구현 방법은 handler 자체를 잠그는 것이 아니라, **언제나 최대 하나의 배수
   루프(drain)만이 달리고 있는** 것입니다: `running` 플래그가 그 "논리적
   잠금"입니다;
2. **handler가 예외를 던져도 strand는 죽지 않습니다**. 배수 루프는 예외 경로에서
   `running`을 해제하고, 남은 handler를 재스케줄하며, 그 후 예외를 계속
   위로 던집니다 — strand의 직렬화 보증은 예외를 삼키지 않고, 예외로 인해도
   작동 불능이 되지 않습니다.

`running`은 `std::mutex::lock` 같은 블로킹 의미론이 아닙니다: strand를 얻지
못한 handler는 기다리지 않고 **큐에 들어갑니다** — 이것이 "비동시"와 "호출자를
막지 않는다"의 양립점입니다. 큐잉 자체는 strand 내부의 mutex(미세 입도, 매우
짧은 임계 구역)가 보호하며, 진짜 직렬화는 `running`이 보증합니다.

## 한 번의 배수의 생애

`s.post(h)`부터, h의 생애는 4단계입니다:

1. **입대**: h가 `queue`에 들어갑니다. 잠금 내에서 `running`을 검사합니다:
   배수 루프가 달리고 있지 않으면, true로 하고 "배수를 스케줄할 필요가 있음"을
   기록합니다;
2. **스케줄**: scheduler에 익명 함수를 `post`합니다 — 그 함수는 1가지만 합니다:
   `drain()`을 호출하는 것. 이 익명 handler는 보통의 handler처럼 큐에 들어가고,
   실행되고, work 단위를 점유합니다. scheduler에게 strand는 아무런 특수성도
   없습니다;
3. **배수**: `drain()`은 루프입니다 — 잠금 내에서 `queue`에서 하나 꺼내고,
   잠금 밖에서 실행하며, 비어 있으면 `running`을 false로 하고 반환합니다.
   실행 중에 새로 post된 handler는 큐에 들어갈 뿐이며, **같은** 루프가 다음
   주기에서 계속 꺼냅니다;
4. **마무리**: 큐가 비고, `running`을 false로 합니다. 다음 post는 "여유 있는"
   strand를 보고, 단계 1부터 다시 시작합니다.

이 형태의 묘미는 3단계에 있습니다: **배수 루프가 "실행권"과 "큐"를 묶습니다**.
`running`이 true인 동안, 전 세계의 `s.post`는 큐에 추가하는 것에 지나지
않습니다 — 실행권은 drain의 손에 있으며, 순서는 자연히 post 순서입니다
(불변식 1의 후반).

`drain`의 예외 처리에 독립적으로 볼 가치가 있습니다:

```cpp
try
{
  execute(std::move(handler));
}
catch (...)
{
  reschedule_after_exception();
  throw;                          // the exception keeps flying out of run()
}

void strand::impl::reschedule_after_exception()
{
  bool more = false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    running = false;
    if (!queue.empty())
    {
      running = true;
      more = true;
    }
  }
  if (more)
    schedule_drain();
}
```

예외가 날아갈 때 `running`이 아직 설정되어 있습니다 — 처리하지 않으면,
strand는 영원히 잠깁니다. `reschedule_after_exception`은 먼저 해제하고, 남은
handler를 재배수(새로운 1회의 `post`)하며, 마지막에 `throw`로 예외가 asio의
규칙대로 `run()` 밖으로 날아갑니다. 테스트 `strand_test`의 예외 구간이
검증하는 것은 정확히 이것입니다: 예외를 던지는 handler 2개가 정상 handler 3개를
끼고, 3회의 `run()` 후에 count == 3, exception_count == 2입니다.

## dispatch의 2경로: 1개의 공유 판정

strand에는 `post`와 `dispatch`의 2개 입구가 있습니다. `post`는 단순합니다
(항상 큐에 넣습니다); `dispatch`의 의미론은 "할 수 있으면 그 자리에서"입니다 —
**하지만 무엇을 "할 수 있다"고 하는가는, strand와 scheduler의 대답이
일치해야 합니다**. 코드 먼저, `src/strand.cpp`의 `impl::dispatch`입니다:

```cpp
if (current_strand_impl == this)        // already inside this strand: inline
{
  execute(std::move(handler));
  return;
}

bool run_now = false;
bool schedule = false;
{
  std::lock_guard<std::mutex> lock(mutex);
  // asio: can_dispatch() && !locked_
  if (scheduler.can_dispatch() && !running)
  {
    running = true;
    run_now = true;                     // inline execution
  }
  else
  {
    queue.push_back(std::move(handler));
    if (!running)
    {
      running = true;
      schedule = true;                  // idle strand + outside run: queue + drain
    }
  }
}
// run_now → execute + drain; schedule → schedule_drain()
```

3개의 분기, 판정은 1문입니다: **호출 스레드가 `run()` 안에 있으며, 대상이 현재
비어 있습니다**. scheduler의 `dispatch`는 context를 봅니다
(`current_scheduler == this`), strand는 strand 자신을 봅니다
(`current_strand_impl == this`) — 대상은 다르지만, 원칙은 같습니다. asio의
2개의 구현은 각각 `can_dispatch()`(scheduler.ipp)와
`can_dispatch() && !locked_`(strand_service.ipp)라고 쓰여 있으며, sasio는
양쪽과 동형입니다.

이 "공유 판정"은 태어난 것이 아니라, 고쳐진 것입니다. 초기판의 sasio
`strand::dispatch`는 `running`만을 보고 있었습니다: strand가 비어 있으면 그
자리에서 실행합니다 — **호출 스레드가 `run()` 안에 없어도**. 그래서
`strand s(ctx); s.dispatch(h);`는 `run()`이 시작되기 전에 h를 실행했고, 제2장
계약 2에 직접 위반했습니다. 이 저장소의 검토 프로세스가 그것을 잡았고, 수리는
asio `strand_service::do_dispatch`의 `can_dispatch() && !locked_`로 정렬되며,
회귀 테스트를 보충했습니다:

```cpp
void strand_dispatch_outside_run_test()
{
  io_context ioc;
  strand s(ioc);

  bool ran = false;
  s.dispatch([&ran] { ran = true; });

  ASIO_CHECK(!ran);      // fails before the fix: the old code ran it inline
  ioc.run();
  ASIO_CHECK(ran);       // the handler executed inside run()
}
```

인라인 실행의 3개의 경로를 나란히 놓습니다 — 제2장 계약 1의 총계정으로:

| 경로 | 인라인 조건 | 판정 대상 |
| --- | --- | --- |
| `scheduler::dispatch` | `current_scheduler == this` | context |
| `strand::dispatch` | `can_dispatch() && strand 유휴` | strand |
| `strand::dispatch`(이미 strand 내) | `current_strand_impl == this` | strand |
| 타이머 만료(`run_one_impl`) | run 루프가 직접 호출, 큐를 경유하지 않음 | —— |

네스트 의미론도 이 표 안에 있습니다: 이미 strand를 쥔 스레드가 다시 `dispatch`
하면, 반드시 그 자리에서 실행됩니다(asio와 동일 — 테스트
`increment_with_lock`이 단언하는 것이 바로 그것입니다).

## 정독: thread_local의 2회의 스택 출입

`execute`와 `running_in_this_thread`는 1개의 thread_local 변수를 공유합니다:

```cpp
void execute(std::function<void()> handler)
{
  void* previous = current_strand_impl;
  current_strand_impl = this;          // push

  struct restore
  {
    void* previous;
    ~restore() { current_strand_impl = previous; }   // pop
  } guard = {previous};

  handler();
}
```

저장하는 것은 "이 스레드가 어느 strand 위에 있는가"가 아니라 **스택**입니다 —
`previous`가 외층을 저장하고, 소멸자가 복원합니다. 왜 스택이 필수인가?
handler 안에서 **다른** strand의 handler를 `dispatch`할 수 있기 때문입니다:
내측 strand의 `execute`는 변수를 자신으로 바꾸고, 끝날 때 외층으로
복원합니다. 중첩된 strand의 `running_in_this_thread`는 이것으로 매 층에서
정확하게 대답합니다.

asio와의 대조: asio는 포인터를 저장하지 않고, strand의 impl을 handler의
associated executor에 붙이며, `running_in_this_thread`는 executor의 호출
스택 비교에 따릅니다. thread_local 스택은 executor 없는 전제에서 sasio의
최소 등가물이며, 의미론은 일치합니다: **"나는 어느 strand 위에 있는가"는
실행 시의 동적 사실이고, 컴파일 시의 귀속이 아닙니다**.

## 대조표

| 질문 | asio | sasio |
| --- | --- | --- |
| 형태 | `strand<Executor>` executor 어댑터 + `detail/strand_service` | io_context에 결합된 객체 + `impl` |
| 배수 | `strand_impl::run()`(operation 완료가 구동) | `drain()` 루프 |
| 인라인 판정 | `can_dispatch() && !locked_` | `can_dispatch() && !running` |
| 중첩 인식 | executor 호출 스택 | `thread_local` + 저장/복원 |
| 결합 문법 | `bind_executor(strand, handler)` | `s.post(...)` / 수동 랩(아래) |
| 소스 입구 | `strand.hpp`, `detail/impl/strand_service.ipp` | `src/strand.cpp` |

"자동으로 상속할 수 없다"는 sasio 차이표에서 가장 가치 있는 1조입니다: asio의
`async_read(socket, buf, bind_executor(strand, h))`는 **합성 연산의 내부의
모든 중간 완료**를 strand에 통과시킵니다. sasio의 수동 랩은 명시적으로 감싼
그 1단만을 감쌉니다. `examples/ex04_bind_executor.cpp`가 랩의 모습을 10줄로
보여주고, 그것이 할 수 없는 것도 보여줍니다 — 이 구멍의 원리(associated
executor의 검색 체인)는 제12장의 주제입니다.

## 연습

**실험**: `tests/asio/strand.cpp`의 멀티스레드 케이스를 실행하고, 파괴
실험을 합니다 — `strand_multithread_test`의 `s.post(...)`를 모두
`ioc.post(...)`로 바꾸고(strand를 우회하여), 다시 실행합니다. `max_active`
어서션의 결과를 보십시오: strand를 쓰지 않으면, 2개의 `run()` 스레드가
병행 실행하는 handler의 피크는 1이 아니게 됩니다. 이 대조가 strand의 전체
가치이자, "어떤 상태에 strand의 보호가 필요한가"의 판별 실험입니다.

**버그 사냥**: 이 장의 서두에 언급한 strand dispatch 버그는 저장소의 역사
안에 누워 있습니다. `tests/asio/strand.cpp`에서
`strand_dispatch_outside_run_test`를 찾아, 이름을 바꾸어 실행되지 않게 하고,
`src/strand.cpp`의 dispatch 인라인 조건을 `scheduler.can_dispatch() &&
!running`에서 일시적으로 `!running`으로 되돌려(초기판의 편차를 복원하여),
재빌드하고 실행합니다. 2개의 어서션 중 어느 것이 먼저 실패합니까? 실패할 때,
handler는 어느 스레드에서 실행되었습니까(디버거로 호출 스택을 보십시오 —
스택의 밑바닥은 `strand_dispatch_outside_run_test`이고 `run`이 아닙니다)?
올바른 구현으로 되돌립니다. 여러분은 이 저장소의 검토 프로세스의 첫 번째
벌레 잡기를 스스로의 손으로 재현한 것입니다.

---

직렬화가 보충되었으므로, 콜백식의 asio는 완성입니다. 마지막 현대적인
피스가 남았습니다: 콜백을 직선의 코드로 쓰는 것 — `co_await`. 다음 장은
`awaitable<T>`과 `co_spawn`을 분해합니다: promise, symmetric transfer,
그리고 "완료 handler가 소멸자 안에 숨어 있는" 마무리 설계.
