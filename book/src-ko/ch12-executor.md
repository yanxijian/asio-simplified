<!-- translated-from: book/src/ch12-executor.md sha256:c0e46a7ada0b -->


# executor 계층: 완료 전달 정책

제10장은 갈고리를 남겼습니다: sasio의 strand는 **명시적으로 넘긴** handler만
직렬화하지만, `async_write` 내부의 중간 완료는 자동으로 따라가지 않습니다 —
asio에서는 이것을 "자동 상속"이라 부르며, executor 계층의 일입니다. 이 장은
잘려나간 이 계층을 다룹니다: 무엇을 해결하는지, 기제는 어떤 모습인지, 그리고
언젠가 sasio에 다시 장착한다면 최소한 무엇을 설치해야 하는지.

이전 11장과 달리, 이 장의 "정독" 대상은 진짜 asio의 심볼입니다 — sasio에는
대응물이 없습니다. 그것이 바로 Part V의 읽기 방식입니다: asio의 구현을 상대로
개념을 이해하고, sasio의 부재를 대조로 사용합니다.

## 문제: 전달 정책은 일급 요구입니다

콜백식 비동기에는 지금까지 조용히 건너뛴 질문이 있습니다: handler는 **어느
스레드에서, 어떤 방식으로** 실행되는가? 엔진 3장의 대답은 늘 소박했습니다 —
"`run()` 스레드에서, 큐에서 꺼낸다". 단일 스레드에서는 문제없습니다; 멀티
스레드 `run()`이 시작하는 순간, 이 대답은 부족해집니다. 3개의 실제 시나리오:

1. **직렬화**: 한 session의 읽기와 쓰기 handler가 동시에 실행되어서는 안
   됩니다 — 그것이 strand이지만, 합성 연산의 모든 중간 완료도 strand에 들어가야
   합니다. 그렇지 않으면 잠금이 우회됩니다;
2. **오프로딩**: 해결된 데이터의 무거운 처리가 I/O 스레드를 점유해서는 안
   됩니다 — 어떤 handler가 "다른 곳에서 실행되고" 끝나면 돌아오기를
   바랍니다;
3. **일반화**: 실행 장소를 하나 추가할 때마다(스레드 풀, strand, 사용자 정의
   스케줄러), socket 타입이 그에 따라 인스턴스화를 다시 해야 합니다 — 타입
   폭발입니다.

이 3가지를 일반화하면: **"어디서, 어떻게 달리는가"는 handler의 일급 속성이어야
합니다**, 엔진에 직접 쓰인 규칙이 아니라. 이 속성이 executor입니다.

## 설계: 3문장의 프레임워크

asio의 executor 프레임워크는 3문장으로 압축됩니다:

1. **모든 handler에는 associated executor가 있습니다**. 어디에서 오는가? 2단계
   조회 체인입니다: handler 타입이 자신의 것을 가지고 있으면
   (`bind_executor(strand, h)`가 만드는 바인딩 객체), 그것을 사용합니다;
   없으면 발기 측이 넘긴 기본값을 사용합니다 — I/O 객체는 발기 시 자신의
   executor를 이 기본값으로 전달하며, I/O 객체의 executor 기본값은
   `any_io_executor`로, 구축 시 받은 것(보통 io_context의)을 감싸고 있습니다;
2. **전달 동작은 3종입니다**: `post`(결코 인라인이 아니며, 항상 큐에 넣음),
   `dispatch`(인라인 가능 — 제10장의 2개 판정 조건이 그 구현입니다),
   `defer`(post와 유사하지만, 완료 체인이 "직전에 전달된 계속"인 경우
   앞질러 가지 않고, 엔진에 배치 처리의 기회를 줍니다);
3. **strand는 executor 어댑터입니다**. `strand<Executor>`는 내부 executor를
   감싸며, `post`/`dispatch`는 여전히 내부로 향하고, 큐잉과 직렬화만
   추가됩니다 — 그래서 strand 위의 스레드 풀, strand 위의 strand도
   조합됩니다.

sasio는 1의 불완전한 버전(`thread_local current_scheduler`가 "폴백" 역할),
2의 절반(post/dispatch, defer 없음), 3의 객체 버전(executor가 아니라 context에
결합된 strand)을 가지고 있습니다. 빠져 있는 것은 정확히 "정책을 매개변수로
만드는" 그 절반입니다.

## trace: 한 번의 바인딩이 전파되는 모습

executor의 가치는 제8장의 합성 연산에서 가장 명확합니다. asio의 이 한 줄의
전달 체인을 보십시오:

```cpp
asio::async_read(socket, asio::buffer(buf),
    asio::bind_executor(strand, handler));
```

`bind_executor`가 만드는 것은 바인딩 객체입니다: handler와 strand의
패키지. 그 후:

1. `async_read` 내부에서 `read_op`가 조립되고, 최종 완료 handler는 이 바인딩
   객체입니다;
2. 모든 층의 **중간 완료**(`read_op`가 끝나고 다음 짧은 구간을 발기할 때)는,
   엔진에 전달할 때 현재 handler의 associated executor를 묻습니다 — 바인딩
   객체는 strand를 가지고 있습니다;
3. 따라서 모든 중간 완료도 strand를 통해 큐에 들어가고, **체인 전체가
   직렬화됩니다**.

1단계에서 1회 발기하고, 2단계에서 층마다 유효합니다 — "1회 바인딩, 어디까지나
유효"의 기제는 **조회 체인이 handler와 함께 이동**하는 것이지, "누가
호출하는가"와 함께 이동하는 것이 아닙니다. sasio의 ex04 수동 랩은 최외층
lambda만 감싸고, 내측 완료(있는 경우)는 새어 나갑니다 — sasio의 handler는 벌거
벗은 `std::function`이므로, "내 executor는 이것"이라는 속성을 걸 곳이
없기 때문입니다.

3가지 전달 동작의 차이를 시나리오에 못 박습니다: I/O 완료는 post(인라인
불가 — reactor 콜백의 스택 위에 있고, 재입은 위험합니다); 사용자가 handler 안에서
호출하는 `dispatch`는 인라인 가능(이미 handler의 스택 위에 있으므로, 전달을
1회 절약합니다); 완료 체인의 계속은 defer(인라인이 아니지만, 엔진에 "저는
계속입니다"라고 알려, 엔진이 열 뒤에 줄을 서는 대신 즉시 이어서 실행할 수
있습니다 — 캐시 국소성의 최적화 여지).

## 정독: 조회 체인과 타입 소거

이 계층을 지탱하는 2개의 asio 심볼. 첫째, **조회 체인의 구현**,
`asio/associated_executor.hpp`:

```cpp
template <typename T, typename Executor = inline_executor>
struct associated_executor
{
  using type = typename detail::associated_executor_impl<T, Executor>::type;

  static type get(const T& t, const Executor& d = Executor())
  {
    return detail::associated_executor_impl<T, Executor>::get(t, d);
  }
};
```

`detail::associated_executor_impl`은 2단계 판별입니다: T가 자신의
`executor_type`을 가지고 있으면 그것을 사용하고(`executor_type` 특수화), T가
직접 `associated_executor`를 특수화했으면 역시 그것을 사용하고(`void_t`
특수화), 그렇지 않으면 호출자가 넘긴 기본값 `d`로 폴백합니다(주 템플릿).
주의: 체인은 2단계뿐입니다 — "I/O 객체의 executor"는 체인의 한 단계가
아니라, **호출자가 넘기는 기본 인수**입니다: socket은 연산을 발기할 때
자신의 executor를 `d`로 전달합니다. 이 "trait + 폴백"의 형태는 asio의
곳곳에 있습니다 — associated_allocator, associated_cancellation_slot도
같습니다. asio를 읽으며 "겹겹이 쌓인 trait"을 보면, 이것이 골격입니다.

둘째는 **타입 소거**, `asio/any_io_executor.hpp`:

```cpp
class any_io_executor :
  public execution::any_executor<
      execution::context_as_t<execution_context&>,
      execution::blocking_t::never_t,
      execution::prefer_only<execution::blocking_t::possibly_t>,
      execution::prefer_only<execution::outstanding_work_t::tracked_t>,
      execution::prefer_only<execution::outstanding_work_t::untracked_t>,
      execution::prefer_only<execution::relationship_t::fork_t>,
      execution::prefer_only<execution::relationship_t::continuation_t>
    >
```

주의할 것은 그것이 **클래스**이며, `execution::any_executor<...>`를 상속한다는
점입니다(프로퍼티 이름은 `blocking_t::never_t` 같은 중첩 형식), 7개의
프로퍼티가 "블록 금지", "work 카운트", "계속 관계"를 조회·수정 가능한 키로
바꿉니다. 이러한 프로퍼티를 만족하는 임의의 executor를 담을 수 있는 소거
컨테이너입니다: 내측은 소형 객체 최적화 저장소, 호출은 가상 함수 표를
통과합니다. 1.74부터 모든 I/O 객체의 기본 executor 인수입니다 —
`basic_stream_socket<tcp, any_io_executor>`의 1개 타입이 io_context,
thread_pool, strand를 모두 커버합니다. 대가는 명시적입니다: 전달마다 1단의
간접 호출, 소형 객체에 들어가지 않을 때는 1회의 힙 할당. asio는 템플릿 폭발을
이 타입 소거로 바꿨습니다. sasio는 io_context가 1개뿐이므로, 이 층조차
필요하지 않습니다.

## 3열 대조: 결여 ↔ 구현 ↔ 장소

| 능력 | sasio 현황 | asio 구현 | asio 장소 |
| --- | --- | --- | --- |
| 지정한 실행체로의 완료 전달 | 없음(handler는 벌거벗은 `std::function`, 전달은 scheduler 고정) | associated executor 조회 체인 + `bind_executor` | `asio/associated_executor.hpp`, `asio/bind_executor.hpp` |
| strand 직렬화 | 있음(context에 결합된 객체) | executor 어댑터, 조합 가능 | `asio/strand.hpp`, `detail/impl/strand_service.ipp` |
| 스레드 풀로 오프로드 | 없음 | `thread_pool`(그 자체가 executor) | `asio/thread_pool.hpp` |
| 실행체의 실행 시 선택 | 없음(io_context 1개) | `any_io_executor` 타입 소거 | `asio/any_io_executor.hpp` |
| 실행 프로퍼티(블록 금지, 계속 배치 등) | 없음 | `execution::` 프로퍼티 프레임워크 | `asio/execution/*.hpp` |
| 계속 전달(defer) | 없음 | `defer` + continuation 의미론 | `asio/defer.hpp` |
| 인라인 전달 | 있음(2개 판정 조건, ch10) | `can_dispatch()` + `dispatch` | `detail/impl/scheduler.ipp` |

"sasio 현황"이 "있음"인 행의 구현은, 이전 장에서 모두 분해했습니다 — 엔진의
2행(strand, dispatch)은 정확히 executor 프레임워크 안에서 **매개변수화 없이도
성립하는** 부분입니다. 이것이 역방향으로 sasio의 판단을 설명합니다: 엔진의
몸통(ch3–11)은 executor 없이도 자기 완결적이며, 그것을 필요로 하는 것은
모두 "일반화"의 수요입니다.

## 연습

**실험**: ex04의 수동 랩으로 "자동 상속 없음"을 증명합니다. ex04 위에
2단 도약 시나리오를 조립합니다:

```cpp
auto bound = bind_strand(strand_, [&](error_code) {
  std::puts("first hop: strand=1");
  const auto hop1_thread = std::this_thread::get_id();

  timer_.async_wait([&](error_code) {      // hop two: unwrapped
    const bool in_strand = strand_.running_in_this_thread();
    const bool same_thread = std::this_thread::get_id() == hop1_thread;
    std::printf("second hop: strand=%d, same_thread=%d\n",
        in_strand ? 1 : 0, same_thread ? 1 : 0);
  });
});
timer_.async_wait(bound);
```

예측: hop 1의 `running_in_this_thread()`는 얼마입니까? hop 2는? 실행하여
비교하세요 — hop 2는 strand에서 벗어났습니다. 수동 랩이 감싼 것은 hop 1의
완료뿐이기 때문입니다. 이것이 "bind_executor는 1회 바인딩으로 어디까지나
유효"의 부정적 교재이자, asio가 조회 체인을 handler 체계에 조립한 이유입니다.

**버그 사냥**: `asio/strand.hpp`를 열고, 이 어댑터의 인터페이스 면을
세어 보십시오: 4개의 전달 멤버(`post` / `dispatch` / `defer` /
`running_in_this_thread`) 외에, `query` / `require` / `prefer`가 있습니다 —
executor 프로퍼티 프로토콜의 3인조입니다. `sasio::strand`가 빠진 부분을 이
멤버들에 대해 열거하고, 답하세요: `query`가 없어서, 어떤 asio 코드가 이식
불가능해집니까? (힌트: strand에 그 아래의 context를 묻는
`asio::query(s, execution::context)` 같은 코드.)

---

executor는 "어디서 달리는가"에 답했습니다. 이 계층의 다른 절반 — "결과를
어떻게 돌려주는가" — 가 남아 있습니다. completion token과 그 `async_result`
체계는 다음 장에서 분해합니다.
