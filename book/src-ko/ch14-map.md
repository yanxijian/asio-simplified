<!-- translated-from: book/src/ch14-map.md sha256:eb220720e315 -->

# 진짜 asio 읽기 지도

제13장까지, 잘려나간 2계층 — executor와 completion token — 이 모두 분해되었으며,
sasio의 9장(ch3–11)은 엔진과 조합 계층을 각각 한 번씩 걸었습니다. 이제 이 책의
시작 약속 중 마지막 하나에 답합니다: 이 이해를 가지고, 169,063줄의 진짜 asio를
어떻게 읽을 것인가.

이 장은 6단 골격을 면제하고 3부 구성을 사용합니다: 소스 트리의 **계층 관례**
(레이아웃을 알면 방황이 절반으로 줄어듭니다), **6주 읽기 계획**(명확한 주간
목표, 검증 가능), 그리고 **심볼 대응 목록** — sasio의 모든 개념이 asio 안의
어디에 착지하는가. 기술 버전은 1.38.2로 고정입니다.

## 계층 관례: 디렉터리가 설명서

`include/asio`에는 4가지 관례가 있습니다. 이것을 따르면 파일 찾기의 헤매는
절반으로 줄어듭니다:

1. **`asio/xxx.hpp`는 파사드이고, `asio/impl/xxx.hpp`가 구현입니다**.
   `io_context.hpp`의 1,384줄은 무섭게 보이지만, 대부분은 문서 주석과
   `#include`입니다. 진짜 로직은 `detail/impl/scheduler.ipp`(745줄)에 있습니다.
   `impl/`을 보면 "구현이 여기 살고 있다"는 것을 알 수 있습니다;
2. **`detail/`은 엔진이고, `asio/`는 API입니다**. `detail/` 아래의 코드는
   자유롭게 리팩터링·개명·플랫폼 분기할 수 있으며 공개 API에 영향을 주지
   않습니다. `asio/` 아래의 모든 헤더는 약속입니다. 제3~11장에서 대조한
   scheduler, reactor, timer_queue, reactive_socket_*_op는 모두 `detail/`에
   있습니다;
3. **`*_fwd.hpp`는 선언만 합니다**. 전방 선언이 헤더 의존을 끊기 위해 많이
   사용됩니다. "불완전 타입"으로 읽기가 막히면, `*_fwd.hpp`에서 진짜 선언이
   어느 헤더에 있는지 찾으십시오;
4. **파일이 플랫폼별로 나뉩니다**. 동일한 이름의 `epoll_reactor` /
   `kqueue_reactor` / `select_reactor` / `dev_poll_reactor` /
   `win_iocp_io_context` / `io_uring_service`는 하나의 개념의 플랫폼별
   백엔드입니다. 하나를 통독하면 6개 모두를 알게 됩니다 — 같은 인터페이스
   (reactor 의미론 또는 IOCP 의미론)를 구현하고 있기 때문입니다.

2가지 "파일이 아닌 조직": **operation 객체**
(`detail/scheduler_operation`)는 asio의 만능 봉투입니다 — handler, 할당자,
work 단위, 취소 상태를 하나의 힙 위의 객체에 묶어, op_queue에서 줄을 서고,
scheduler가 실행한 후 파괴합니다. sasio는 멤버 슬롯 + `std::function`으로
대체했으며, 그 등가와 대가는 제7, 8장에서 다루었습니다. **handler tracking**
(`detail/handler_tracking.hpp`, 90줄)은 디버깅 도구입니다:
`ASIO_ENABLE_HANDLER_TRACKING`을 정의하면 발기와 완료의 모든 것이 로그로
남습니다(`@asio|타임스탬프|동작`) — "이 handler가 정말 달렸는가" 질문에는
디버거보다 빠릅니다.

## 6주 읽기 계획

주당 2~4개 파일, 4~8시간. 매주 명확한 완료 기준이 있습니다. 기준을 놓치면 그
주에 머물러 있고 — 서두르지 마십시오.

**1주: io_context와 scheduler**(本書 ch3–4와 병행)

- 읽기: `asio/io_context.hpp`(1,384줄, 문서 주석 건너뛰기),
  `detail/impl/scheduler.ipp`(745줄, 초점은 `do_run_one`);
- 기준: "2개의 스레드가 동시에 `run()`을 호출할 때, 누가 어느 handler를 실행하고,
  `stop()` 후 큐 안의 handler는 어디로 가는가"에 답할 수 있는가;
- 자가 테스트: `outstanding_work_`는 sasio의 4조건과 어떻게 대응하는가?
  sasio의 `maybe_stop_if_idle`은 asio의 어느 줄에 대응하는가?

**2주: reactor(하나의 백엔드를 통독)**(本書 ch5와 병행)

- 읽기: `detail/epoll_reactor.hpp`(314줄) +
  `detail/impl/epoll_reactor.ipp`(838줄, 초점은 `start_op` / `run` / `wait`).
  Linux 이외는 `select_reactor`(sasio의 스냅샷 모델과 가장 가까운 구조);
- 기준: "새 등록이 왜(또는 왜 필요 없는지) 대기 스레드를 깨우는가"와
  "투기적 읽기(speculative read)란 무엇인가"(asio는 준비 상태에서 직접 I/O를
  시도하고, 실패하면 재등록합니다 — sasio의 "꺼내서 재등록"보다 적극적)를
  설명할 수 있는가;
- 자가 테스트: sasio의 `if (first) interrupt()`는 epoll_reactor 안에서 무엇에
  대응하는가? (답: epoll의 커널츠 등록으로 새 descriptor는 자연히 보입니다 —
  대응물은 `epoll_ctl` + op_queue에의 입대이지, 깨움이 아닙니다.)

**3주: socket 연산**(本書 ch7과 병행)

- 읽기: `detail/reactive_socket_recv_op.hpp`(218줄) — 완전한 operation
  객체의 표본; `detail/socket_ops.hpp`(428줄) +
  `detail/impl/socket_ops.ipp`(4,712줄, 필요에 따라 참조. 통독하지 않음);
- 기준: `async_read_some`의 발기부터 완료까지의 객체 그래프를 그릴 수 있는가
  (socket → service → reactor op → scheduler 큐);
- 자가 테스트: operation 객체 안에 handler 외에 무엇이 있는가?
  (할당자, work, 취소 슬롯 — sasio의 슬롯은 어느 것이 없고, 없으면 무엇이
  깨지는가?)

**4주: 합성 연산과 완료 조건**(本書 ch8과 병행)

- 읽기: `asio/read.hpp`(1,553줄 — 발기 함수의 쓰는 방법) +
  `asio/impl/read.hpp`(850줄, `detail::read_op`); `write.hpp` /
  `impl/write.hpp` 대칭(1,532 / 743줄);
- 기준: `transfer_exactly(n)`과 `transfer_at_least(n)`의 판정 차이, 그리고
  완료 조건이 op의 어느 단계에서 질문되는지 설명할 수 있는가;
- 자가 테스트: sasio의 연습에서 쓴 `transfer_at_least(n)`을, asio의
  `base_from_completion_cond`은 어떻게 정책으로 만들었는가?

**5주: strand와 executor**(本書 ch10, ch12와 병행)

- 읽기: `asio/strand.hpp`(579줄) + `detail/impl/strand_service.ipp`
  (211줄 — 짧아서 통독에 값합니다); `asio/associated_executor.hpp`;
- 기준: "strand의 handler는 어느 스레드에서 실행되는가", "dispatch의
  인라인 조건", "sasio의 `can_dispatch() && !running`과 어떻게 대응하는가"에
  답할 수 있는가;
- 자가 테스트: sasio의 `thread_local current_strand_impl`의 asio에서의
  대응물은 무엇인가? (executor의 호출 스택 비교 — `running_in_this_thread`는
  associated executor를 통과합니다.)

**6주: coroutine**(本書 ch11과 병행)

- 읽기: `asio/awaitable.hpp`(163줄) + `asio/impl/awaitable.hpp`
  (1,121줄, 초점은 `awaitable_frame`의 `resume` / `destroy`);
  `asio/co_spawn.hpp`(525줄) + `impl/co_spawn.hpp`(449줄);
- 기준: "coroutine 프레임의 메모리는 누가 관리하는가(사용자 정의
  `operator new`), co_spawn의 work는 누가 관리하는가(`co_spawn_state`의
  guard), `use_awaitable`의 token은 어떻게 완료 handler가 되는가(`async_result`
  특수화 — ch13 참조)"에 답할 수 있는가;
- 자가 테스트: sasio의 `~promise_type`이 completion을 호출합니다 — asio의
  대응하는 동작은 어디인가?

6주 이후의 확장 읽기는 필요에 따라: `detail/handler_tracking.hpp`(90줄,
디버깅), `io_uring_service`(최신 백엔드), `asio/execution/*.hpp`(프로퍼티
프레임워크 전문 — ch12의 심수역), `src/examples/`(공식 예제, 실행 가능,
토픽별 폴더).

## 심볼 대응 목록

sasio를 읽은 후, 이 개념들은 asio에서 "한눈에 알아볼 수 있어야" 합니다:

| sasio 개념 | asio 착지(1.38.2) |
| --- | --- |
| `execution_context` / `service` | `asio/execution_context.hpp` + `detail/service_registry.hpp` |
| `io_context::work` | `executor_work_guard` / `make_work_guard` |
| `scheduler::run_one_impl` | `detail/impl/scheduler.ipp`의 `do_run_one()` |
| `handler_work` | `work_cleanup`(operation 완료 시의 work 해제) |
| `reactor::start_read` / `if (first)` | `select_reactor::start_unready_op`의 `if (first) interrupter_.interrupt()` |
| `timer_queue::add`가 earliest를 반환 | `select_reactor::schedule_timer`의 `if (earliest)` |
| `timer_queue::pop_at` | `detail/timer_queue.hpp`의 힙 삭제(이 버그는 없음. asio는 per_timer_data 리스트) |
| `start_read_some` / `do_read` | `basic_stream_socket::async_read_some` → `detail/reactive_socket_recv_op.hpp` |
| `read_op` / `write_op` 릴레이 | `impl/read.hpp` / `impl/write.hpp`의 `detail::read_op` / `write_op` |
| `can_dispatch() && !running` | `strand_service::do_dispatch`의 `can_dispatch() && !locked_` |
| `drain()` 루프 | `strand_impl::run()` |
| `awaitable<T>`의 `promise_type` | `impl/awaitable.hpp`의 `awaitable_frame` |
| `co_spawn`의 `top_level` + completion | `impl/co_spawn.hpp`의 `co_spawn_state` |
| resolver의 짧은 수명 스레드 | `detail/resolver_thread_pool.hpp`(상주 풀) |

이 목록은 또한 이 책의 검수 기준입니다: **모든 행에 대해, 이 책을 열지 않고도
asio 소스에서 직접 찾을 수 있는가** — 그 시점에 이 책은 역할을 다 했습니다.

## 마무리

sasio는 asio의 축소판이 아니라, 그 **설명의 운반체**입니다: 엔진 장(ch4–6)은
asio가 왜 멈추지 않고, 왜 공회전하지 않는지를 보여주고, I/O 장(ch7–9)은 발기와
완료가 왜 2상으로 나뉘는지를 보여주며, strand와 coroutine 장(ch10–11)은 콜백의
2대 고충이 어떻게 해결되는지를 보여주고, Part V(ch12–14)는 잘려나간 일반화
기계를 하나씩 돌려줍니다.

읽은 후에 돌아올 가치가 있는 것은 2가지뿐입니다: asio 소스 자체(유일한
진실의 원천), 그리고 연습에서 쓴 여러분의 예측 — 특히 틀린 것들. 비동기
프로그래밍의 어려움은 결코 API가 아닙니다; "동시에 일어나는 두 가지 사이에서
무엇이 일어나는가"입니다. 이 책이 가르친 모든 것은, 그 "사이"를 더 명확하게
보게 하는 것을 목표로 했습니다.
