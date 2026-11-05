<!-- translated-from: book/src/appendix-de.md sha256:770fd03bce4f -->

# 부록 D: 연습 답안

각 장의 연습 완전 답안. 먼저 스스로 하십시오 — 답은 도망가지 않습니다.

## ch1 실험: 빌드와 테스트

명령은 본문과 같습니다. 흔한 실패: Ninja 부재 시 기본 제너레이터로
(실행 파일이 `build\Debug\`로 이동); `ws2_32` 링크 오류는 VS 커맨드
환경에 없다는 의미입니다.

## ch1 버그 사냥: 예외 케이스의 3문

1. 예외를 던진 handler는 **이미 디큐되어 있었으며**, 예외와 큐는 무관합니다.
   정상 handler는 그대로 보존되어 catch 후 재실행에서 계속됩니다;
2. `stopped()`는 **false**입니다. 예외는 정지 지시가 아닙니다. 정지는 "네 조건
   모두 빈 것"이나 명시적 `stop()`에서만 발생합니다. 예외가 날아갈 때 큐에
   아직 일이 있었으므로, 플래그는 한 번도 설정되지 않았습니다;
3. 각 `run()`이 최소 1개의 handler를 처리한 후 예외로 중단됩니다; 3개의 정상
   handler는 3주에 분산되어 실행되고, 그 후 큐가 비고, 네 조건이 맞고,
   `run()`이 0을 반환하면 `break`가 조건을 충족합니다.

## ch2 실험: echo 서버의 병행

2개의 연결이 동시에 처리됩니다: socket마다 reactor의 descriptor 테이블에
등록되어, 1회의 `WSAPoll`이 복수의 준비를 보고하고, 루프가 순서대로 처리합니다.
연결만 하고 보내지 않는 클라이언트는 준비를 생산하지 않으며, 시스템 콜을
낭비하지 않습니다.

## ch2 버그 사냥: cancel의 통보 시기

handler는 `cancel()`의 호출 스택 안에서 실행되지 않습니다 — `post` 후에
run 루프가 실행합니다. 오류 코드는 `operation_aborted`입니다. 만약 호출
스택 안에서 동기적으로 호출되고, handler가 호출자가 유지하는 상태에
닿으면, 재입입니다. "취소는 반드시 post를 통과한다"가 이 경로를
제거합니다.

## ch3 실험: handler_stats 서비스

2개의 context가 각각 `handler_stats`를 걸어도 간섭하지 않습니다: 불변식 1의
완전한 표현은 "서비스 타입별, **context**별로 최대 1개" — 서비스의 키는
`(context, type)`이지, 글로벌 type이 아닙니다.

## ch3 버그 사냥: recursive_mutex

일반 `std::mutex`로 바꾸면, 교착은 **두 번째** `use_service` 진입에서
발생합니다: `io_context` 구축 → `use_service<scheduler>()`가 잠금을 취득 →
scheduler가 구축 → `use_service<reactor>()`가 같은 잠금을 다시 취득. 스택
트레이스에는 2단의 `use_service` 프레임과 1곳의 `std::mutex::lock`이
보입니다. 재귀적 조립 체인은, 필요에 따른 조립의 직접적인 대가입니다.

## ch4 실험: work 누출

붙잡힌 루프: `work_count_`는 1(guard가 보유), handler가 달린 후 큐는
비어 있고, 타이머 힙은 비어 있으며, descriptor 없음 — 그러나
`has_work_locked()`는 `work_count_ != 0`이므로 참이고, `run_one_impl`의
매 주기는 3단계까지 진행하여, "아직 일이 있다"고 계산한 후
`reactor_.run_for(max)`에서 잠듦으로, 아무도 깨우지 않습니다. `reset()`판:
2번째 context의 4조건은 `reset()` 후 모두 성립하며, `run()`은 **0**을
반환합니다.

## ch4 버그 사냥: 예외의 3문

제1장의 답을 보십시오(같은 케이스). 요점 복습: 디큐된 handler는 큐와
분리되며, 예외는 정지 플래그를 세우지 않고, 3개의 정상 handler는 3주에
분산되어 실행됩니다.

## ch5 실험: 첫 번째 누락의 재현

2곳의 `if (first) interrupt()`를 주석 처리하면,
`tcp_multithread_wakeup_test`가 약 5초에 실패합니다: run 스레드는
`WSAPoll(∞)`에서 잠들어 있고, 새로 등록된 읽기는 그 스냅샷에 없습니다.
유일한 깨움원은 5초 후의 워치독 타이머입니다. 복원하면 즉시 통과합니다.
교훈: 스냅샷 모델의 새로운 대기원은, 대기자를 반드시 깨워야 합니다.

## ch5 버그 사냥: spurious readiness

2가지 원인: **다른 독자가 먼저 소비한**(같은 데이터그램을 다른 `recvfrom`이
가져가거나, 엔진의 투기적 읽기가 먼저 I/O를 했거나), 또는 **가짜 경보
그 자체**(poll은 "아마도 읽을 수 있다"만 약속합니다). TCP의 재전송이나
체크섬은 보고된 준비를 사라지게 하지 않습니다 — 원인 목록에 없습니다.
재등록하지 않는 경우: 콜백은 이미 표에서 나갔으며(불변식 2), 이 읽기는
reactor의 세계에서 사라집니다 — 버퍼는 다음 준비 알림을 영원히 받지 못하고,
**조용한 읽기 정지**가 됩니다.

## ch6 실험: pop_at의 범위 외

재현 포인트: 2개의 타이머(50ms / 300ms). 먼저 만료되는 것이 `pop_at(0)`을
실행하고, 힙 크기 2에서 `index(0) != last(1)`이므로, 스왑 경로가 삭제된
entry의 `heap_index`를 1로 씁니다. `pop_back` 후 힙 크기는 1이 되며, 더러운
인덱스 `1 == size()`가 확정됩니다. 단언은 **그 후**에 발화합니다: 2개의
타이머 객체가 파괴될 때 `cancel()` → `remove()`가 더러운 인덱스를 힙에
적용하여, Debug STL의 `vector subscript out of range`가 튀어나옵니다.
발생 위치는 오류 발생 지점에서 멀리 떨어져 있습니다 — 인덱스형 버그의 전형적
특징입니다.

## ch6 버그 사냥: remove 실패의 3가지 이유

- 인덱스가 `npos`: entry는 이미 힙 밖 — 나가는 길은 하나
  (`take_expired` 또는 `pop_at`)이며, 어느 쪽도 handler의 완료를
  배치했습니다(성공 완료는 scheduler가 직접 호출하여, 전달 중에 있습니다);
- 인덱스 범위 외: entry는 이미 힙 밖(위와 같음). 또는 더러운 인덱스
  (수리 후에는 존재할 수 없음. 방어 검사가 후방 지원);
- 신원 불일치(`heap_[index] != e`): 인덱스가 다른 entry에 속합니다 — 힙 내의
  교환은 `swap_at`이 인덱스를 동기화하므로, 일어나서는 안 됩니다. 방어로
  보존됩니다.
  3가지 경우 모두 handler의 완료에는 이미 수취인이 있어서, 잃어버리지
  않습니다.

## ch7 실험: 2개의 session 교차

교차합니다. 각 session의 `async_read_some` 완료는 독립된 큐 항목입니다.
2개의 socket이 동시에 준비되면, `WSAPoll`이 양쪽을 보고하고, `do_read`가
순서대로 같은 스레드에서 실행되며, 2개의 완료 handler가 차례로 큐에 들어가
실행됩니다. 단일 스레드 `run()`의 병행 모델은 "교차"이지 "병렬"이 아닙니다.

## ch7 버그 사냥: close 후의 새 발기

`close()`는 이미 `handle_`을 `INVALID_SOCKET`으로 만들었으므로, 새로운
`async_read_some`은 `start_read_some`의 `!is_open()` 분기를 통과합니다:
handler는 `bad_descriptor`를 받아 `post`됩니다. 순서: 먼저 투척된
`operation_aborted`가 큐의 앞에 있고, 후에 투척된 `bad_descriptor`가 뒤에
있습니다 — **큐의 FIFO가 순서를 보장합니다**. 오래된 통보(aborted)와 새로운
통보(bad_descriptor)가 모두 도착하며, 각각의 버퍼 유효성은 호출자의
책임입니다.

## ch8 실험: 상태는 write_op로 이동했습니다

수동판의 `write_offset_`/`write_size_`는 session의 멤버였습니다 — session마다
1세트, 생존 기간은 session과 동일; 합성판의 대응하는 상태는 `write_op`의
`transferred_`입니다 — `async_write`마다 1개, 생존 기간은 shared_ptr
릴레이가 관리하며, 체인 끝에서 자동 파괴됩니다. session이 가질 필요가 없는
이유는, 그것이 이 1회의 쓰기 연산에만 속하기 때문입니다.

## ch8 확장: transfer_at_least(n)

`read_op` / `write_op`에 `least_` 멤버를 추가하고, 완료 판정을
`transferred_ == buffer_.size()`에서
`transferred_ >= least_ || transferred_ == buffer_.size()`로 변경합니다.
`least_ = 0`과 기본의 관계는, asio의 **실제 순서**에 따릅니다
(`impl/read.hpp`의 `read_op`): 호출될 때마다 **먼저** 완료 조건을 검사합니다
(`operator()` 시작의 `check_for_completion`); I/O 후에 0바이트를 읽으면
**직접 break하여 eof로 완료**합니다(I/O 후의 0바이트 분지), 두 번째 완료
조건을 통과하지 않습니다. 따라서: `least_ = 0`이면 발기 전에 조건이 성립하여,
성공의 0바이트를 반환합니다 — 그러나 **I/O가 1번도 없는 것은 아닙니다**:
완료 조건이 통과한 후에도 루프는 1회의 제로 길이의 `async_read_some`을
발기하고, 이후 0바이트 분지가 완료시킵니다. 데이터는 움직이지 않습니다.
EOF 시나리오 `least_ > 0`은 eof 오류를 보고합니다. 완료 조건의 검사를 I/O
이전에 배치해야, 이 순서에 맞추어집니다.

## ch9 실험: UDP echo와 병행

`async_send_to`가 미완료인 동안, 다음 `async_receive_from`은 이미
등록되어 있습니다 — 2방향이 독립 슬롯을 점유하여 **병행하여 진행될 수
있습니다**. TCP판과의 차이(session의 동일 방향은 직렬 — 쓰기가 끝나야
다시 읽는 세션 논리)는 UDP에 스트림이 없기 때문입니다: 수신과 송신은
서로 의존하지 않으며, 엔진은 양방향을 독립적으로 등록합니다. 예의
`payload` 복사는 정확히 이 때문입니다. 공유 버퍼는 다음 데이터그램에
덮어씁니다.

## ch9 버그 사냥: 2개의 타이밍

- 타이밍 A: 샘플링은 `getaddrinfo`가 돌아온 후, post 전에
  행해집니다 — `cancel()`이 이미 플래그를 뒤집었으므로, 샘플은 true →
  handler는 `operation_aborted`를 받습니다(해결 결과는 버려집니다);
- 타이밍 B: 샘플링 후에 `cancel()` — 플래그의 반전은, 이미 캡처된
  `aborted`의 값에 닿지 않습니다 → handler는 해결 결과를 받습니다.
  샘플링이 **스레드 안에서** 행해지는 이유: post는 비동기입니다. handler
  안에서 샘플링했다면, "취소가 도착한"과 "샘플된"의 전후는 의미론이 아니라
  스케줄링 지연으로 결정되며, 취소 통보는 무의미하게 1개의 큐 주기만큼
  늦어지고, 타이밍 B도 취소로 오인될 수 있습니다.

## ch10 실험: strand를 우회하는

`s.post`를 `ioc.post`로 바꾸면, handler는 scheduler 큐에 직접 들어가고,
2개의 `run()` 스레드가 각각 꺼내어 실행하며, `max_active`의 최대치는 1을
넘습니다(정확한 값은 스케줄링에 따라 변동, 보통 2). strand판의 일정한 1:
`running` 플래그가 병행을 직렬로 압축합니다. "어떤 상태에 strand가
필요한가"의 판별: 복수의 handler가 읽고 쓰고, 리퀘스트가 병행하여 도착할
수 있는 상태입니다.

## ch10 버그 사냥: dispatch 편차의 재현

`!running`판에서, `strand_dispatch_outside_run_test`의 **첫 어서션**이 먼저
실패합니다(`ASIO_CHECK(!ran)`) — handler가 `s.dispatch`의 호출 스레드
(메인 스레드)에서 인라인 실행되었기 때문입니다. 디버거에서 스택을 바닥에서
읽으면: 테스트 함수 → `strand::impl::dispatch` → `execute` → handler이며,
스택 안에 `run` / `run_one_impl`이 **없습니다** — handler는 run 루프 안에
있지 않았습니다. `scheduler.can_dispatch() && !running`으로 복원하면,
양쪽 어서션이 모두 통과합니다.

## ch11 실험: 재귀 체인과 스택 깊이

재귀 `chain(1000)`은 스택을 한 번도 증가시키지 않습니다: 각 층이
서스펜드하면 제어권은 엔진에 돌아가고, 자식이 완성하면 symmetric transfer가
부모로 이동합니다 — 1,000회의 상승은 1,000회의 이동이지, 1,000개의 쌓인
호출이 아닙니다(디버거에서 가장 깊은 곳에 브레이크포인트를 놓으면, 스택은
수 프레임만 보입니다). `final_suspend`를 `suspend_always`로 바꾸면: 가장
깊은 프레임이 서스펜드한 후 아무도 재개하지 않으며(톱 레벨이 아니고,
completion도 없음), 전체 체인이 동결합니다. 가장 깊은 타이머는 여전히 힙을
점유하지만, 다른 층은 영원히 재개를 기다릴 수 없습니다 — `run()`은 타이머가
완료된 시점에서 4조건이 비어서 정지하며, 테스트는 하악 또는 타임아웃됩니다.
이것이 "아무도 수집하지 않는 프레임은 누출입니다"입니다.

## ch11 버그 사냥: move-only

컴파일 오류의 키워드는 **rvalue 참조 / `operator co_await() &&`**입니다 —
awaitable은 rvalue만 받아들입니다. 올바른 철자:
`co_await std::move(task);`입니다. move-only가 복사를 허용하는 것보다
안전한 이유: 복사된 2개의 awaitable이 같은 coroutine 핸들을 공유하고, 양쪽이
co_spawn되면 — 첫 spawn이 프레임을 가져가고 completion을 설정하며, 두
번째 spawn이 completion을 덮어쓰고, 실행 중이거나 파괴된 프레임을 다시
resume합니다. 소유권이 유일해야 파괴권도 유일합니다.

## ch12 실험: 2개의 hop

hop 1: `running_in_this_thread()`는 1(수동 랩이 효과적). hop 2: 0
— 랩이 감싼 것은 hop 1의 완료 lambda뿐입니다. hop 2의 `timer.async_wait`
완료에는 strand 정보가 없으며, 임의의 run 스레드에 도달합니다. asio의
`bind_executor`는 strand를 handler의 associated executor에 쓰므로, 조회
체인이 층마다 유효합니다 — 이것이 sasio의 수동 랩과 asio 체계의 차이입니다.

## ch12 버그 사냥: strand의 인터페이스 면

asio `strand.hpp`에는 4개의 전달 멤버 외에, `query` / `require` /
`prefer`(executor 프로퍼티 프로토콜의 3인조)가 있습니다. 없으면, 다음이
이식되지 않습니다: `asio::query(strand, execution::context)`(strand에 그
아래의 context를 묻는), `require(strand, execution::blocking.never)`
(실행 프로퍼티의 설정), 및 strand를 executor 컨셉을 만족하는 객체로
제너릭 코드에 넘기는 쓰는 방법 전반.

## ch13 실험: read_with_future

10줄 구현의 본질: promise와 future의 1회의 인도이며, handler는 promise의
`shared_ptr`를 생존 유지합니다. asio가 형식마다 `read_with_xxx`를 쓰지
않는 이유는, `async_initiate`가 "token을 어떻게 완료 handler로 만드는가"를
`async_result` 특수화에 위임하고 있기 때문입니다 — 발기 구현은 1회만
작성되며, 새로운 token 타입은 자신의 특수화를 가져오면 됩니다.

## ch13 버그 사냥: use_awaitable의 재개자

`asio/use_awaitable.hpp`의 특수화에서, 완료 handler는 awaitable frame에의
참조를 가진 객체입니다. 호출되면, 결과를 frame에 쓰고 `frame->resume()`을
호출합니다 — coroutine은 `co_await` 지점에서 계속합니다. sasio의 대응물은
`async_operation` 콜백 내의 `awaiting_.resume()`(제11장 단계 ⑤)입니다.
동명의 형제의 간략판입니다.

# 부록 E: asio와의 의도적 차이 총표

장별로 정리합니다. 정식 리스트는 이 저장소 README의 `Deliberate limits`
섹션입니다(4언어판을 동기 유지). 이 표는 이 책이 전개한 항목과, asio의
어디에서 찾을지를 모은 것입니다.

| 장 | 차이 | 이유 | asio 장소 |
| --- | --- | --- | --- |
| 전체 | Windows + WSAPoll만 | 교육 범위 | `detail/` 아래 6 백엔드 |
| 전체 | executor / completion token 없음 | 조합 계층은 엔진이 아님 | ch12–13의 대조표 |
| ch4 | handler는 `std::function`(operation 객체 아님) | 할당자/취소 훅을 생략 | `detail/scheduler_operation` |
| ch4 | 타이머와 descriptor는 work 단위를 점유하지 않음 | 카운트는 큐의 handler만 | `scheduler.ipp`의 `outstanding_work_` |
| ch5 | interrupter는 자기 연결 UDP | 핸들 쌍을 아낌 | `detail/select_reactor.hpp`의 interrupter |
| ch6 | 1타이머 1대기 | 대기 링크드 리스트의 유지를 생략 | `detail/timer_queue.hpp`의 per_timer_data |
| ch6 | 타임아웃은 scheduler가 계산 | 타이머 큐를 독립된 service로 | `epoll_reactor`의 `update_timeout` |
| ch7 | 방향당 1슬롯(operation 큐 없음) | 객체 관리를 생략 | `reactive_socket_recv_op` |
| ch7 | socket은 이동 불가, accept는 참조 전달 | move 의미론을 생략 | `basic_stream_socket` |
| ch7 | 옵션은 `no_delay`만 | 충분함 | `detail/socket_option*` 전체 |
| **ch9** | **UDP 절단은 `WSAEMSGSIZE`를 삼키지 않고, 오류로 전달** | **POSIX 정렬의 오류 흡수를 실시하지 않음**(검토에서 발견된 행위 차이) | `detail/impl/socket_ops.ipp`의 `recvfrom`에 의한 `WSAEMSGSIZE` / `ERROR_MORE_DATA` 처리 |
| ch9 | resolver는 질의마다 스레드, 취소는 최신만 | 최소 이해 가능판 | `detail/resolver_thread_pool.hpp` |
| ch9 | `udp::endpoint`가 `tcp::endpoint`를 재이용 | 타입 체조를 생략 | `asio/ip/udp.hpp` |
| ch8 | 합성 연산은 단일 버퍼, 완료 조건 하드코딩 | BufferSequence/정책 기저를 생략 | `impl/write.hpp` + `base_from_completion_cond.hpp` |
| ch8 | `async_read_until` 없음 | 동적 버퍼+구분 검색은 연습 수준 | `asio/read_until.hpp` |
| ch10 | strand는 객체이고, executor 어댑터가 아님 | 적응할 executor 체계가 없음 | `asio/strand.hpp` |
| ch10 | `bind_executor`의 자동 상속 없음 | 동상 | `asio/bind_executor.hpp` |
| ch11 | coroutine에 token 없음, 완료는 `~promise_type`에서 | `async_result` 체계를 생략 | `impl/awaitable.hpp`, `impl/co_spawn.hpp` |
| ch11 | `co_spawn`은 직접 `resume()`(동기 완료는 run 외 가능) | 설계대로 제시, 차이 등록 | `impl/co_spawn.hpp`의 배달 시작 |

굵은 행(ch9의 UDP 절단)은, 검토 프로세스가 발견하고 등록한 행위 차이로,
README의 `Deliberate limits` 리스트와 이 표는 동기화되어 있습니다.
