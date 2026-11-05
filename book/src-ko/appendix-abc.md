<!-- translated-from: book/src/appendix-abc.md sha256:9634cbc50de5 -->

# 부록 A: 용어집

출현 순서대로, 모두 영어 원명을 유지합니다(제1장의 약속). 각각 한 문장으로
정의합니다:

| 용어 | 한 문장 정의 |
| --- | --- |
| synchronous call | 반환하면 결과가 손안에 있음. 함수 반환은 끝이다 |
| reactor | 커널은 "누가 준비되었는가"만 보고하고, 프레임이 I/O를 직접 수행(select/poll/epoll/WSAPoll) |
| proactor | I/O를 커널이 수행하고, 콜백이 받는 것은 결과 그 자체(Windows IOCP) |
| handler | 비동기 연산의 완료 함수. 결과 또는 오류를 가지고 있음 |
| initiating function | 발기 함수(`async_read_some` 등). 등록만 하고 실행하지 않음 |
| `io_context` | 서비스 컨테이너 + `run()` 루프의 호스트 |
| service | context에 걸린 엔진 부품(scheduler, reactor, timer queue) |
| work(단위) | "아직 할 일이 있다"의 카운트 단위. 카운트 0과 3개의 대기원이 모두 비면 stop |
| work guard | `io_context::work` / `executor_work_guard` — 의도적으로 유지하는 work 단위 |
| op_queue / operation | asio의 만능 봉투: handler + 할당자 + work + 취소 상태의 힙 위 객체 |
| completion signature | 완료 시그니처. `void(error_code, size_t)`처럼 handler의 형태를 결정 |
| executor | "handler가 어디서, 어떻게 달리는가"의 일급 추상(ch12) |
| associated executor | handler가 가진 executor 속성. 조회 체인으로 기본값에 폴백 |
| completion token | "결과를 어떻게 돌려받는가"의 컴파일 시간 정책(ch13) |
| `async_result` | token의 특수화 표: 발기 함수의 반환 타입과 handler 형태를 결정 |
| strand | 직렬화 보증: 동일 strand의 handler는 병행하지 않고 순서를 유지 |
| drain | strand의 "꺼내기 → 실행 → 또 꺼내기" 루프 |
| interrupter | 깨움 관: 1바이트를 쓰고 poll에서 잠든 스레드를 깨움 |
| spurious readiness | poll이 읽을 수 있다고 했는데 `recv`가 `would_block`을 반환하는 가짜 경보 |
| symmetric transfer | coroutine 간에 제어권을 스택을 쌓지 않고 이동 |
| operation aborted | 취소된 완료의 오류 코드. 취소 ≠ 통보 없음 |
| proactor 모방 | 준비 후 프레임이 I/O를 보태고, 결과를 "완료"로 포장 |

# 부록 B: asio 소스 트리 안내

`include/asio`(1.38.2, 600 `.hpp`, 169,063줄)를 위에서 아래로 4계층으로.
필요에 따라 파고드십시오:

```text
asio/
├─ *.hpp                 public API facades (io_context, socket, read, strand…)
├─ impl/*.hpp            those facades' implementations (read.hpp → impl/read.hpp)
├─ ip/、ssl/、ts/        protocols and extensions (ip/tcp, udp, resolver…)
├─ execution/*.hpp       the executor property framework (blocking, outstanding_work…)
└─ detail/               the engine interior (refactor zone; public API ignores its shape)
   ├─ scheduler.ipp      handler queue and run loop (745 lines)
   ├─ *_reactor*         platform I/O backends (epoll/kqueue/select/dev_poll/
   │                     win_iocp/io_uring + null/spin stubs)
   ├─ reactive_socket_*_op  socket operation objects (recv_op 218 lines and up)
   ├─ socket_ops.ipp     Winsock/BSD thin wrappers (4,712 lines)
   ├─ timer_queue*       the timer heap
   ├─ strand_service     strand draining (211 lines)
   └─ handler_tracking   initiation/completion logs (90 lines, ASIO_ENABLE_HANDLER_TRACKING)
```

3가지 파일 찾기 요령: ①`asio/xxx.hpp`에서 막히면 `asio/impl/xxx.hpp`로.
②엔진 행동은 `detail/impl/*.ipp`로. ③동명의 `*_reactor*`는 플랫폼별로 1개를
읽으면 나머지도 따라옵니다.

## 부록 C: 테스트 스위트 안내

sasio의 9개 테스트 파일(`tests/asio/`)은 **asio 자체 테스트에서 이식**되었습니다
(1.38.2의 `src/tests/unit/`). 파일 헤더에 출처와 삭제를 기재. 이것은 형식이
아닙니다 — 제6장의 힙 범위 외 버그와 제10장의 dispatch 의미론 편차는, 모두
이 테스트가 잡은 것입니다.

| 테스트 파일 | asio 출처 | 커버 범위 |
| --- | --- | --- |
| `io_context.cpp` | `unit/io_context.cpp` | 큐, work, stop/restart, 예외 전파 |
| `steady_timer.cpp` | `unit/deadline_timer.cpp` | 만료, 체인, 취소, 크로스 스레드 깨움 |
| `ip_tcp.cpp` | `unit/ip/tcp.cpp` + 공식 echo 예 | no_delay, echo, 취소, 크로스 스레드 깨움 |
| `strand.cpp` | `unit/strand.cpp` | 중첩 dispatch, 예외 회복, 멀티스레드 직렬화, run 외 dispatch |
| `read.cpp` / `write.cpp` | `unit/read.cpp` / `write.cpp` | 부분 읽기/쓰기 릴레이 |
| `ip_udp.cpp` | `unit/ip/udp.cpp`(echo 케이스로 개작) | send_to / receive_from |
| `resolver.cpp` | `unit/ip/basic_resolver.cpp`(어서션 스타일 참조) | 동기 해결, localhost 루프백 |
| `coroutine.cpp` | `unit/co_spawn.cpp` / `awaitable.cpp`(모델 참조) | co_spawn 결과/예외, 타이머 서스펜드, socket 세션 |

harness(`unit_test.hpp`)에는 알 가치가 있는 2개의 방어선이 있습니다:

1. **어서션은 다이얼로그를 띄우지 않습니다**: Debug CRT의 어서션은 보통
   Abort/Retry/Ignore 다이얼로그를 내고, Ignore를 누르면 범위 외 프로그램이
   계속 달리며 "passed"까지 인쇄합니다. harness는 `_CrtSetReportHook2`를
   설치하여 어서션을 stderr + `_Exit(3)`으로 바꿉니다 — 즉사, CTest는 확실히
   빨간색입니다;
2. **오류 카운터**: `ASIO_CHECK`는 중단하지 않고 세며, 스위트 끝에 카운트로
   비제로를 반환합니다 — 1회 실행으로 모든 실패가 보이고, 첫 번째 것만
   보이는 것이 아닙니다.

스위트 전체 실행: `cmake --build build && ctest --test-dir build
--output-on-failure`(9건, 약 10초). 제5, 6장의 2개의 크로스 스레드 깨움
회귀 테스트는, 판별력이 있는 테스트 설계의 모범입니다. 수리가 결핍되면
확실한 실패(그리고 붙잡히지 않음)가 됩니다.
