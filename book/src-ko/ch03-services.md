<!-- translated-from: book/src/ch03-services.md sha256:966ab72561e0 -->

# io_context가 왜 이렇게 얇은가: 서비스 레지스트리

제2장은 풀리지 않은 질문 하나를 남겼습니다: `async_read_some`이 발기할 때,
버퍼와 handler를 socket에 저장하고 reactor에 관심을 등록해야 합니다. 그런데
socket은 reactor를 어디서 얻는가? reactor, 타이머 힙, handler 큐 같은 부품들은
누가 만들고, 누가 소유하고, 언제 파괴되는가?

가장 쉬운 답은 전부 `io_context`에 넣는 것입니다:

```cpp
class io_context {
  Scheduler scheduler_;    // handler queue
  Reactor   reactor_;      // descriptor table
  TimerHeap timers_;       // timers
  // ...one more line here for every new feature
};
```

동작은 합니다. 그런데 asio의 `io_context`를 보십시오 — 수백 줄의 헤더에서
구체적 기능에 속한 줄은 거의 없고, "reactor 멤버"를 찾을 수 없습니다. 게으른
것이 아니라, 이 문제를 다른 층에 넘긴 것입니다: **서비스 레지스트리**. 이 계층의
존재가 `io_context`가 왜 이렇게 얇을 수 있는지를 설명합니다.

## 설계: 필요할 때 스스로 조립되는 컨테이너

sasio의 답은 두 타입에 있습니다. `execution_context`가 컨테이너이고,
`service`가 부품의 기반 클래스입니다:

```cpp
class service {
public:
  virtual ~service() = default;
  virtual void shutdown() {}   // called at context destruction, reverse order
};

class execution_context {
public:
  template <typename T, typename... Args>
  T& use_service(Args&&... args);          // return if present, create if not

protected:
  void shutdown_services();                // reverse-order shutdown, then destroy
};
```

불변식 두 개, 각각 하나를 지킵니다:

1. **서비스 타입당, context당 최대 하나의 인스턴스**. 첫 번째
   `use_service<reactor>()`가 생성하고, 이후 모든 호출은 같은 인스턴스를
   반환합니다. 먼저 온 사람이 생성을 발기합니다 — socket 생성자여도, scheduler
   생성자여도, 사용자 코드여도 됩니다;
2. **종료는 생성의 역순으로 진행됩니다**. 나중에 만들어진 서비스가 먼저
   shutdown합니다. scheduler가 생성할 때 reactor를 만들었으므로, 정리할 때는
   reactor가 먼저 정리되고 scheduler가 나중에 정리됩니다 — 의존하는 쪽이 먼저
   떠나고, 의존되는 쪽이 뒤를 정리합니다.

`io_context`는 `execution_context`를 상속하고 딱 한 가지만 합니다: 생성 시
컨테이너에 scheduler를 요구합니다. 큐, reactor, 타이머 힙은 모두 서비스로서
필요할 때 나타나며, `io_context`는 그 존재를 모릅니다. 기능을 하나 추가하는
것(제10장의 strand, 제9장의 resolver)도 `io_context`의 코드 한 줄 건드리지
않습니다.

## 하나의 생성 재귀 체인

"필요할 때 생성"이 가장 흥미로워지는 순간은 `io_context`의 생성자입니다:

```cpp
io_context::io_context()
  : scheduler_(use_service<scheduler>())   // first service in the container
{
}
```

이 한 줄이 촉발하는 생성은 한 번 이상입니다. scheduler는 reactor와 타이머 힙을
관장하므로, 그 생성자도 컨테이너에 두 서비스를 요구합니다:

```cpp
scheduler::scheduler(execution_context& ctx)
  : reactor_(ctx.use_service<reactor>()),   // recursion: create the reactor
    timers_(ctx.use_service<timer_queue>()) // recursion: create the timer heap
{
}
```

따라서 첫 번째 `io_context`의 생성 시퀀스는: scheduler 등록 → scheduler 생성 →
reactor 등록 → reactor 생성(Winsock 초기화, interrupter 열기) → timer_queue 등록 →
생성 완료 → scheduler로 복귀 → io_context로 복귀. 하나의 재귀 체인이며, 조립
순서가 곧 의존 순서입니다 — 어떤 "전역 초기화 목록" 같은 배치도 필요 없습니다.
필요한 사람이 발기합니다.

같은 체인이 불변식 2가 왜 중요한지도 설명합니다. 생성 순서가 scheduler →
reactor → timer_queue라면, shutdown 순서는 timer_queue → reactor →
scheduler입니다: reactor가 먼저 정리되고(interrupter와 Winsock을 닫고),
scheduler가 나중에 정리됩니다(큐를 비우고, 디스패치를 멈추고), 마지막에
컨테이너가 객체를 일괄 파괴합니다. 순서가 거꾸로라면, scheduler의 shutdown 안에
있는 `reactor_.interrupt()`가 이미 shutdown된 interrupter에 부딪히게 됩니다.

## 정독: use_service의 20줄

이 계층의 핵심은 하나의 함수입니다. `include/sasio/execution_context.hpp`의
구현입니다(주석은 원문 영어 그대로):

```cpp
template <typename T, typename... Args>
T& use_service(Args&&... args)
{
  static_assert(std::is_base_of<service, T>::value,
      "a sasio service must derive from sasio::service");

  std::lock_guard<std::recursive_mutex> lock(mutex_);   // ①

  const std::type_index key(typeid(T));                 // ②
  const auto existing = services_.find(key);
  if (existing != services_.end())
    return static_cast<T&>(*existing->second);          // ③

  std::unique_ptr<T> owned(new T(*this, std::forward<Args>(args)...));
  T& created = *owned;                                  // ④
  services_.emplace(key, std::move(owned));
  order_.push_back(&created);
  return created;
}
```

네 곳, 각각 이유가 있습니다:

- **① 재귀 뮤텍스**. 일반 `mutex`는 ④에서 교착합니다 — `new T(*this, ...)`가 T의
  생성자를 호출하고, scheduler의 생성자가 다시 `use_service<reactor>()`를
  호출하며, 같은 함수에 재진입해 같은 잠금을 다시 잡기 때문입니다.
  `recursive_mutex`는 한 스레드의 재진입을 허용해, 재귀적 조립 체인이 통과할 수
  있게 합니다. 이것이 "필요할 때 조립"의 직접적인 대가입니다;
- **② type_index를 키로**. `typeid(T)`를 `std::type_index`로 감싸면 해시
  테이블에 들어갑니다 — "scheduler 서비스"와 "reactor 서비스"는 자연히 다른
  키입니다. asio도 typeid를 쓰지만, 자체 관리 구조로 감싸고 있습니다;
- **③ 히트하면 반환**. 불변식 1의 "최대 하나"가 여기서 실현됩니다 — 두 번째
  요청은 같은 객체의 참조를 받습니다;
- **④ context가 생성자로 들어갑니다**. 모든 서비스의 첫 생성 인자는 `*this`
  이므로, 서비스는 역으로 컨테이너를 사용할 수 있습니다(scheduler가 정확히 이
  방법으로 reactor를 얻습니다). 생성 후 `order_`에 기록합니다 — 역순 shutdown이
  걷는 근거입니다.

한 곳 더 멈출 만합니다: `services_`는 `unique_ptr<service>` 기반 포인터를
저장하고, `use_service`는 반환 전에 다운캐스트합니다. 다형성이 "어떤 서비스가
존재하는가"에 대한 지식을 전부 호출 지점으로 밀어내고, 컨테이너 자신은 무지를
유지합니다 — 이것이 `io_context`가 얇은 메커니즘입니다.

## 대조표

| 질문 | asio | sasio |
| --- | --- | --- |
| 컨테이너 | `execution_context.hpp` | `include/sasio/execution_context.hpp` |
| 레지스트리 | `detail/service_registry.hpp` | `use_service` 템플릿 자체 |
| 서비스 기반 | `execution_context::service` | `sasio::service` |
| 생성 | `make_service` + typeid 키 | `use_service` + typeid 키 |
| 재귀적 생성 | 지원(service_registry가 재진입) | 지원(`recursive_mutex`) |
| 정리 | 역순 shutdown | `order_` 벡터의 역순 |
| asio 전용 | `add_service` / `has_service`, `notify_fork`, `shutdown`과 `destroy`의 분리 | 없음(대응하는 수요가 없음) |

asio의 service_registry는 sasio에 없는 몇 가지 설비를 가지고 있습니다:
레지스트리는 `first_service_` 연결 리스트로 서비스를 연결하고, 사후
`add_service`와 조회 `has_service`를 지원하며, `notify_fork`(프로세스 fork 후
내부 상태 재구축)를 지원하고, "shutdown(자원 해제)"과 "destroy(객체
파괴)"를 두 단계로 나눕니다. 변하지 않는 것: 서비스 타입당, context당 최대
하나 — `basic_io_object.hpp`에서 I/O 객체는 생성 시 `use_service`로 그 공유
서비스를 받고, 각 객체는 자신의 `implementation_`만 소유합니다. "typeid 키 +
필요할 때 생성 + 역순 shutdown"의 골격은 완전히 동일하며, asio의
`service_registry`를 읽을 때 여기의 모든 줄을 알아볼 것입니다.

## 연습

**실험**: 레지스트리를 활용해 자신의 서비스를 작성합니다 — 이 context가
실행한 handler 수를 셉니다. 전체가 30줄 이내입니다:

```cpp
class handler_stats : public sasio::service {
public:
  explicit handler_stats(sasio::execution_context& ctx) : service(ctx) {}
  void shutdown() override { std::printf("handlers: %zu\n", count); }

  void on_ran() { ++count; }

private:
  std::size_t count = 0;
};

// usage: wrap each post
auto& stats = ctx.use_service<handler_stats>();
ctx.post([&stats] { stats.on_ran(); /* your real work */ });
ctx.run();   // prints handlers: 1
```

실행하여 출력된 숫자가 post한 횟수와 일치하는지 확인하세요. 그다음 질문:
두 개의 `io_context`가 각각 `handler_stats`를 걸면, 카운트가 서로
간섭합니까? (힌트: 불변식 1의 "context별"을 다시 읽습니다.)

**버그 사냥**: `execution_context.hpp`의 `recursive_mutex`를 일반 `std::mutex`로
바꾸고, 재빌드하여 ch1의 `ex01_post`를 실행하세요. 무엇이, 어디서 일어날지
예측한 다음 실행하여 비교하세요. 교착을 예측했다면 — 잠금이 몇 번째 진입에서
걸렸습니까? 스택 트레이스의 두 `use_service` 프레임은 각각 누가 호출한
것입니까?

---

서비스 컨테이너가 갖춰졌고, 첫 서비스인 scheduler도 이미 자리에 있습니다.
하지만 지금까지는 "큐 안의 handler가 run()에 의해 실행된다"고 말했을 뿐 — run
루프의 전문, work 카운트의 기장 규칙, "할 일이 없음"의 정확한 판정은 아직
펼치지 않았습니다. 다음 장은 scheduler를 해부합니다. 라이브러리에서 가장
중요한 83줄입니다.
