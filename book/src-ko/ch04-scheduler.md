<!-- translated-from: book/src/ch04-scheduler.md sha256:2eec44ad32d0 -->

# scheduler: handler 큐와 워크 카운트

제3장은 scheduler를 컨테이너 안에 블랙박스로 놓고 마쳤습니다. 이 장이 그 상자를
엽니다. 먼저 이미 사용한 코드부터 — 제1장 연습의 예외 케이스로,
`tests/asio/io_context.cpp`에서 가져온 것입니다:

```cpp
ioc.post(&throw_exception);        // throws int
ioc.post(bind(increment, &count)); // counts normally
ioc.post(&throw_exception);

for (;;)
{
  try
  {
    ioc.run();
    break;                         // only a clean exit breaks out
  }
  catch (int)
  {
    ++exception_count;             // some handler threw an int
  }
}
// afterwards: count == 3, exception_count == 2
```

세 번의 `run()`, 두 번의 예외, 세 개의 카운트 handler가 모두 실행되었습니다.
이 케이스 뒤에는 네 가지 질문이 숨어 있습니다: handler가 예외를 던진 후, 큐에
남은 것들이 여전히 실행되는가? `run()`은 어떻게 "아직 할 일이 있어서, 한 번 더
돌릴 가치가 있다"를 아는가? 예외를 던진 "반쯤 실행된 handler"가 점유한 자원은
누가 회수하는가? 그리고 가장 근본적인 — `run()`은 대체 무엇을 루프하고 있는가?

`scheduler`의 83줄짜리 `run_one_impl`과 약 30줄의 카운트 처리 코드(post 카운트,
`handler_work`, 4조건 판정)가 네 가지 모두에 답합니다.

## 설계: 하나의 카운터가 세 종류의 일을 관리한다

데이터 구조부터. scheduler는 큐, 카운터, 정지 플래그를 가지고 있습니다:

```cpp
std::mutex mutex_;
std::deque<std::function<void()>> queue_;   // handlers waiting to run
std::size_t work_count_ = 0;                // outstanding work units
std::atomic<bool> stopped_{false};
```

불변식 세 개 — 읽기 전에 외워 두세요:

1. **대기 중인 큐 handler마다 하나의 work 단위가 대응하며, 끝나는 즉시
   해제됩니다 — handler가 예외를 던져도 해제됩니다**. 여기서 "대기"는 큐에
   들어간 handler만을 가리킵니다; 타이머와 descriptor는 카운트를 차지하지
   않지만, 역시 정지를 막습니다(불변식 2);
2. **`work_count_`가 0이고, 큐가 비어 있고, 타이머 힙이 비어 있고, 등록된
   descriptor가 없을 때 — 네 조건이 모두 충족되면 scheduler가 스스로 stop합니다**.
   `io_context::work`를 유지하고 있으면 후자 세 개가 비어 있어도 멈추지 않습니다.
   stop 후 큐는 그대로 보존되며, `restart()`가 있어야 재개됩니다;
3. **handler는 큐 앞에서 하나씩 꺼내 실행되며, 한 주기에 최대 하나입니다**.
   실행 전에 스레드 마커가 설정되고(dispatch의 인라인 판정이 이것에 의존),
   예외는 호출 스택을 따라 올라가 `run()` 밖으로 나가며, 여기서 잡히지 않습니다.

불변식 1의 "끝나자마자 해제"는 작은 RAII 클래스가 실현합니다. `handler_work`는
생성에서 아무것도 하지 않고(단위는 `post` 시점에 계상됨), 파괴에서 해제합니다:

```cpp
class handler_work
{
public:
  explicit handler_work(scheduler* s) : scheduler_(s) {}
  ~handler_work() { scheduler_->on_work_finished(); }
  // ...
};
```

이 객체가 살아 있는 기간 = work 단위가 카운트에 남아 있는 기간입니다 — asio의
`work_cleanup` 역할입니다. `handler_work`와 `invoke`를 조합하면: handler에서
예외가 나가도 스택 풀기 동안 `handler_work`의 소멸자는 여전히 실행되고, 카운트는
정확히 감소합니다; 예외는 계속 위로 날아가 `run()` 밖으로 나갑니다. 이것이
asio의 행동입니다: **예외는 호출자의 것, 카운트는 프레임워크의 것**.

## 정독: run_one_impl 전문

이제 83줄의 루프를 분해합니다. 라이브러리에서 유일하게 블록하는 곳이자, 세
서비스(큐, 타이머 힙, reactor)가 만나는 곳입니다. 단계별로.

**입구: run()의 사전 검사.**

```cpp
std::size_t scheduler::run()
{
  if (!has_work())      // all four conditions empty: not worth entering
  {
    stop();
    return 0;
  }
  return run_impl(true);
}
```

할 일이 없으면 즉시 `stop()`하고 0을 반환합니다 — 이것도 asio의 행동입니다.
`run()`이 할 일이 없다고 판단하면 context는 stopped 상태가 되고, 이후에 무언가를
post해도 `restart()`가 있어야 실행됩니다. `poll()`, `run_one()`, `poll_one()`도
같은 검사를 합니다.

**루프 단계 0: 먼저 정지 플래그.**

```cpp
for (;;)
{
  if (stopped())          // once stopped, no further handler is invoked
    return false;
```

정지의 의미는 "즉시 마치고, 큐는 그대로 보존": 이미 디큐된 handler는 마무리되고
(현재 이것), 아직 디큐되지 않은 것은 하나도 움직이지 않으며, `restart()`를
기다립니다. 테스트의 "예외가 나간 후 run()을 다시 돌리면 남은 handler가
실행된다"가 검증하는 것이 바로 이것입니다.

**단계 1: 큐에 handler가 있으면 — 하나 꺼내 하나 실행합니다.**

```cpp
std::function<void()> handler;
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!queue_.empty())
  {
    handler = std::move(queue_.front());
    queue_.pop_front();
  }
}

if (handler)
{
  handler_work work(this);
  invoke(std::move(handler));
  return true;
}
```

잠금의 범위에 주목합니다: 잠금은 "디큐" 동작만 보호합니다. handler의
**실행은 잠금 밖에서** 이루어집니다 — 실행이 잠금을 유지하면, handler 내부의
어떤 `post`(같은 잠금을 필요로 함)도 즉시 교착할 것입니다. `run_one_impl`이
true를 반환하는 것은 "이번 주기에 한 단위의 일을 했다"는 뜻이며, `run_impl`은
`while (run_one_impl(block)) ++count;` 카운팅 셸입니다.

`handler_work`가 여기서 생성됩니다 — 디큐 순간부터 이 handler의 work 단위는
카운트에 남아 있고, 실행이 끝나면(예외 포함) 소멸자가 해제합니다. 불변식 1,
여기서 실현됩니다.

**단계 2: 큐가 비었을 때 — 타이머를 본다.**

```cpp
if (std::shared_ptr<timer_queue::entry> expired =
        timers_.take_expired(clock::now()))
{
  std::function<void(error_code)> complete = std::move(expired->handler);
  if (complete)
  {
    handler_scope scope(this);
    complete(error_code());
  }
  maybe_stop_if_idle();
  return true;
}
```

만료된 타이머의 handler는 run 루프 안에서 **직접 호출됩니다** — 제2장 계약 1을
상기하세요: I/O 완료는 post를 거치고, 타이머 완료는 이 경로를 취합니다.
`handler_scope`가 스레드 마커를 설정하고(dispatch의 인라인 판정이 작동하도록),
`complete(error_code())` — 빈 error_code, "정상 만료"입니다. 호출 후
`maybe_stop_if_idle()`: 이것이 마지막 대기였다면, 실행을 마친 시점에서 멈춰야
합니다.

**단계 3: 두 출처가 모두 비었을 때 — 얼마나 잘 수 있는지 계산합니다.**

```cpp
bool work;
clock::duration timeout = clock::duration::zero();
{
  std::lock_guard<std::mutex> lock(mutex_);
  work = has_work_locked();
  if (work)
  {
    if (!block)                   // poll(): one non-blocking pass only
    {
      if (polled)
        return false;
      timeout = clock::duration::zero();
    }
    else if (timers_.empty())
    {
      timeout = clock::duration::max();   // no timers: sleep indefinitely
    }
    else
    {
      timeout = next timer expiry - now;  // sleep until then
    }
  }
}

if (!work)
{
  maybe_stop_if_idle();           // all four empty: record the stop
  return false;
}

reactor_.run_for(timeout);        // the only place that ever blocks
polled = true;
```

`has_work_locked()`가 불변식 2의 구현이며, 네 조건을 한 번에 봅니다:

```cpp
return work_count_ != 0 || !queue_.empty() || !timers_.empty()
    || !reactor_.idle();
```

수면 시간은 셋 중 하나: 타이머가 없으면 무한히 잠듦(`post`, 새 descriptor
등록, `stop`으로만 깨울 수 있음 — "깨어나는 방법"은 제5장 주제); 타이머가
있으면 가장 이른 만료까지; `poll()` 의미론에서는 비블로킹 1회만(`polled`
플래그가 `poll()`이 reactor 위에서 계속 도는 것을 방지합니다).

마지막 `reactor_.run_for(timeout)`에서 깨어난 후, 제어 흐름은 루프 꼭대기로
돌아갑니다 — 정지 플래그를 다시 보고, 큐를 다시 봅니다. 제2장의 "매 주기마다
세 출처를 모두 다시 검사한다"가 전문에서 바로 이렇게 구현되어 있습니다.

## work의 생애

불변식 1과 2에서 반복적으로 나타나는 work 단위는 태어나서 죽기까지 세 가지
경로만 있습니다:

- **큐**: `post`에서 `++work_count_`; handler 실행이 끝나면 `handler_work`
  소멸자에서 `--work_count_`;
- **가드**: `io_context::work w(ctx);`가 생성에서 `++work_count_`, 소멸(또는
  `reset()`)에서 `--work_count_`. 의미는 "아직 일을 더 넣을 것이니 멈추지 마" —
  전형적 사용은 멀티스레드 서버에서 메인 스레드가 work guard를 유지하여 worker
  스레드가 context를 소진하지 못하게 하는 것;
- **비동기 해결**(제9장): resolver의 워커 스레드가 하나를 유지하여,
  `getaddrinfo`가 돌아오기 전까지 `run()`이 빠져나가지 않습니다.

카운트가 0이 되는 것 자체가 곧바로 stop은 아닙니다 — "네 조건 모두 빈 것"을
성립시키고, `on_work_finished()`는 감소 직후 즉시 `maybe_stop_if_idle()`을
호출하므로, 네 조건이 맞으면 `stopped_`가 그 자리에서 설정되고, run 루프가
다음 주기 시작에서 관찰합니다. asio의 `work_finished()`도 같은 방식으로
동작합니다.

## 대조표

| 질문 | asio | sasio |
| --- | --- | --- |
| 루프 본체 | `detail/impl/scheduler.ipp`의 `do_run_one()` | `src/scheduler.cpp`의 `run_one_impl()` |
| work 카운트 | `outstanding_work_` | `work_count_` |
| 실행 시 카운트 | operation의 `work_cleanup` | `handler_work` RAII |
| 즉시 완료 | `post_immediate_completion()` | 직접 `post` |
| 정지 조건 | `outstanding_work_ == 0`(구축 시 미계상 옵션) | 네 조건(타이머와 descriptor 포함) |
| 가드 | `executor_work_guard` | `io_context::work` |

하나의 의도적 차이를 지적할 가치가 있습니다: asio의 `run()`은 "큐만 있고 타이머가
없는" 판정 경로가 sasio와 다릅니다(asio의 큐 handler도 work로 계상되어 네 조건이
하나로 축소됨; sasio의 타이머와 descriptor는 무계상이므로 별도로 검사합니다).
**관찰 가능한 행동**은 일치합니다: 어떤 종류의 대기라도 있으면 `run()`은
빠져나가지 않습니다.

## 연습

**실험**: work 누수를 만들어 `run()`이 붙잡히는 것을 직접 봅니다:

```cpp
sasio::io_context ctx;
sasio::io_context::work guard(ctx);   // forgot to reset
ctx.post([] { std::puts("done"); });
ctx.run();                            // never returns — why?
std::puts("never printed");
```

실행해 보고(Ctrl+C로 끝내세요). 붙잡힌 루프를 이 장의 언어로 묘사하세요:
네 조건 중 어느 세 개가 비어 있고, 어느 하나가 비어 있지 않으며,
`run_one_impl`의 매 주기가 어느 줄에서 잠드는가. 대조 케이스: 두 번째
context에는 아무것도 post하지 않고, guard를 생성 즉시 `reset()`하면, `run()`은
handler를 하나도 실행하지 않고 **0**을 반환합니다:

```cpp
sasio::io_context ctx2;
sasio::io_context::work guard2(ctx2);
guard2.reset();                       // the unit goes back early
std::size_t n = ctx2.run();           // all four empty → stop, returns 0
std::puts(n == 0 ? "clean exit" : "unexpected");
```

두 context를 나란히 놓으면, "work는 무엇 때문에 run()을 계속 살려 두는가"와
"run()의 반환값은 무엇인가"가 모두 명확해집니다.

**버그 사냥**: 제1장이 남긴 세 가지 질문으로, 이제 스스로 답할 수 있습니다:

1. 첫 handler가 예외를 던질 때, **그것은 이미 디큐되어 있었고**, 예외와 큐는
   무관합니다. 정상 handler는 그대로 보존되어 catch 후 재실행에서 계속됩니다
   — 그래서 count == 3입니다;
2. `stopped()`는 **false**입니다: 예외는 정지 지시가 아닙니다. 정지는 "네 조건
   모두 빈 것"이나 명시적 `stop()`에서만 발생합니다. 예외가 날아갈 때 큐에 아직
   일이 있었으므로, 정지 플래그는 한 번도 설정되지 않았습니다 — 그래서
   catch 안에서 `run()`을 다시 호출하는 것이 합법입니다;
3. 세 번인 이유는 각 `run()`이 최소 한 개의 handler를 처리한 후 예외로 중단되기
   때문입니다; 남은 handler는 후속 라운드에서 계속 실행되어, 세 개가 모두
   실행되고, 큐가 비고, 네 조건이 맞고, `run()`이 0을 반환하면 `break`가
   마침내 조건을 충족합니다.

예측과 대조하세요. 세 개 모두 맞았다면, 이미 구현자처럼 이 루프를 생각하고
있는 것입니다.

---

루프, 카운트, 정지가 모두 갖춰졌습니다. 하지만 3단계의
`reactor_.run_for(timeout)` 한 줄이 여전히 블랙박스입니다 — `WSAPoll`은 어떻게
호출되고, descriptor 테이블은 어떻게 관리되며, 다른 스레드는 잠든 사람을
어떻게 깨우는가? 다음 장은 reactor를 해부합니다 — 거기서 "두 번의 깨움 누락"을
만나게 될 것입니다. 그 한 줄에 묻혀 있던 두 개의 진짜 버그입니다.
