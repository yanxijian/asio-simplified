<!-- translated-from: book/src/ch05-reactor.md sha256:c48f47d82926 -->

# reactor: 준비 상태, 깨우기, descriptor 테이블

제4장의 루프에는 한 줄이 끝까지 열리지 않았습니다:

```cpp
reactor_.run_for(timeout);   // the only place that ever blocks
```

`run()` 시간의 대부분이 이 줄 위에서 잠듭니다. 무엇 위에서 잠들고, 무엇 때문에
깨나는가? 제2장의 시퀀스 다이어그램은 "WSAPoll이 준비를 보고한다"고 했지만,
"보고" 뒤에는 반드시 설명해야 할 문제가 하나 더 있습니다 — **잠든 사람을 어떻게
깨우는가?** 이 장은 `src/reactor.cpp`를 엽니다. 거기서 이 책의 가장 인상 깊은
두 개의 버그를 만나게 될 것입니다. 교육용 사례로 만든 것이 아니라, 이
저장소의 커밋 히스토리에서 실제로 발생하여 테스트에 잡힌 것입니다.

## 설계: 표 하나와, 찌르면 깨어나는 관

reactor에 관련된 멤버는 세 개뿐입니다:

```cpp
mutable std::mutex mutex_;
std::unordered_map<SOCKET, descriptor_state> descriptors_;  // the table
SOCKET interrupter_ = INVALID_SOCKET;                        // self-connected UDP
std::atomic<bool> interrupted_{false};                       // dedup flag
```

**descriptor 테이블**은 reactor의 명부: socket당 한 줄, 이 descriptor에서
"누가 읽기를 기다리고, 누가 쓰기를 기다리는가"를 기록합니다:

```cpp
struct descriptor_state
{
  std::function<void()> read_handler;    // invoked on read-ready (it is do_read)
  std::function<void()> write_handler;   // invoked on write-ready (it is do_write)
  bool empty() const { return !read_handler && !write_handler; }
};
```

여기에 저장되는 것은 socket의 **내부 콜백**
(`[this]() { do_read(); }`)이지, 사용자의 handler가 아닙니다 — 사용자
handler는 `do_read`가 진짜 시스템 콜을 끝내고 결과를 얻은 후, `post`로 큐에
들어갑니다(제2장 계약 1). 방향당 하나의 등록만 허용되며, socket의 "방향당
연산 1개" 규칙과 호응합니다.

**interrupter**는 깨우기 소켓입니다. 자기 주소로 `connect`한 UDP socket으로 —
안에 1바이트를 쓰면 즉시 "읽기 가능"이 됩니다. 이것을 `WSAPoll` 감시 목록에
섞어 두면, 잠든 poller를 깨우려는 스레드는 1바이트만 쓰면 됩니다. asio는
POSIX에서 socket pair, eventfd 지원 Linux에서 eventfd, Windows에서 socket
pair를 사용합니다 — sasio는 자기 연결 UDP로 핸들 한 쌍을 아끼며, 효과는
같습니다.

불변식 2개를 여기에 놓고, 이 장 후반에서 계속 돌아올 것입니다:

1. **poll 집합은 스냅샷입니다**. `WSAPoll`이 보는 descriptor 목록은 호출
   순간에 테이블에서 베껴낸 것입니다. 베낀 후에 등록된 새 descriptor는 이번
   주기에서 보이지 않습니다. 따라서 **대기 집합을 바꾸는 새 등록은 잠든
   스레드를 반드시 깨워야 합니다**, 새 사본을 강제로 만들기 위해서;
2. **준비된 등록은 표에서 꺼낸 후에 실행합니다**. `WSAPoll`은 레벨 트리거
   (수평 트리거)입니다. 데이터가 읽히지 않는 한, 다음 poll은 같은 descriptor를
   또 보고합니다. 그래서 준비된 콜백은 표에서 꺼낸 후에 실행합니다 — `do_read`
   의 `recv`가 가짜 경보(spurious readiness)로 아무것도 읽지 못하면, 등록을
   다시 하고 대기로 돌아갑니다.

## 한 번의 깨움의 생애

`post`는 세 가지 깨움 경로 중 가장 흔한 것입니다. `scheduler::post`부터 잠든
스레드가 눈을 뜨기까지, 다섯 단계:

1. 스레드 B가 handler를 큐에 넣고, 잠금을 나와, `reactor_.interrupt()`를
   호출합니다;
2. `interrupt()`는 먼저 원자 플래그 `interrupted_`를 봅니다: 이미 true라면
   누군가 방금 찌른 것이고 바이트가 아직 날아가는 중이므로, 그대로 반환합니다
   (**중복 제거** — 천 번의 post가 1바이트로 압축됩니다);
3. 플래그가 false→true로 뒤집히고, interrupter에 1바이트를 `send`합니다;
4. 스레드 A는 `WSAPoll`에서 잠들어 있고, poll 목록의 마지막 항목이
   interrupter이므로, 바이트가 도착하는 즉시 반환합니다;
5. A는 준비된 descriptor를 처리한 후, interrupter의 버퍼를 **읽어 없앱니다**
   (`recv` 루프로 빌 때까지) 그리고 플래그를 false로 되돌립니다 — 다음 주기는
   다시 잠들고, 다음 찌르기를 기다립니다.

중복 제거 플래그가 이 기제의 교묘한 부분입니다: 깨움은 **레벨** 의미론(버퍼에
바이트가 있으면 읽을 수 있음)이고, `post`는 **에지** 이벤트(handler마다 한 번
찌름)입니다. 플래그가 없으면, poll의 읽어 없애기와 `interrupt`의 쓰기가 서로
경주합니다. 플래그가 있으면, 여러 post가 하나의 깨움으로 압축됩니다 — 일어난
후 루프 꼭대기가 큐를 **통째로** 다시 보기 때문에, 한 번 찌르는 것과 열 번
찌르는 것은 차이가 없습니다.

## 케이스 스터디: 두 번의 깨움 누락, 처음부터 옳았던 하나

세 가지 깨움 경로(post, 첫 descriptor 등록, 새 최초 타이머)의 기원은 같지
않습니다. 하나는 첫 버전부터 올바르게 쓰였고, 두 개는 테스트에 잡혔습니다.
이 대조를 이해하면, "스냅샷 모델"의 전체 대가를 알게 됩니다.

**처음부터 옳았던 것: post.** `scheduler::post`는 첫 버전부터 조건 없이
`reactor_.interrupt()`를 호출했습니다 — 큐가 변했으니, 잠든 스레드는 깨어서
봐야 합니다. 이 줄이 결핍된 적은 한 번도 없습니다. 큐와 같은 함수 안에 있어서,
눈에 띄게 결합되어 있기 때문입니다. 가르치는 것: **상태를 바꾸는 곳에서, 그
자리에서 깨운다**.

**첫 번째 누락: 첫 descriptor 등록.** 제2장이 reactor에 descriptor 테이블을
추가했을 때, `start_read`는 이랬습니다:

```cpp
void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  std::lock_guard<std::mutex> lock(mutex_);
  descriptors_[descriptor].read_handler = std::move(handler);
  // no wake-up. The sleeping thread knows nothing of this.
}
```

싱글스레드는 완벽합니다 — `async_read_some`을 발기하는 스레드가 run 루프
자신이고, 등록이 끝나면 다음 주기에서 자연히 스냅샷을 다시 베낍니다.
멀티스레드에서는 발각됩니다: 스레드 A가 `WSAPoll`에서 잠들어 있고(타이머 없음,
무한 타임아웃), 스레드 B가 새 socket에 `async_read_some`을 발기합니다 —
등록은 테이블에 들어갔지만, A가 가진 스냅샷은 낡은 것입니다. 타이머도 post도
구원하러 오지 않습니다. **이 읽기 연산은 세계의 종말까지 기다립니다**.

asio의 `select_reactor`는 답을 `start_unready_op`에 써 두었습니다:

```cpp
bool first = op_queue_[op_type].enqueue_operation(descriptor, op);
scheduler_.work_started();
if (first)
  interrupter_.interrupt();     // ← what sasio was missing
```

sasio의 수리(`src/reactor.cpp`)는 이 형태를 그대로 옮깁니다:

```cpp
void reactor::start_read(SOCKET descriptor, std::function<void()> handler)
{
  bool first = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    descriptor_state& state = descriptors_[descriptor];
    first = !state.read_handler;      // empty→non-empty in this direction = first
    state.read_handler = std::move(handler);
  }

  // A thread already blocked in WSAPoll() is waiting on a snapshot of the
  // descriptor set taken before this registration, so it must be woken up to
  // rebuild that set. asio does exactly this in
  // select_reactor::start_unready_op():
  //   if (first) interrupter_.interrupt();
  if (first)
    interrupt();
}
```

`first` 판정은 잠금 내에서 이루어집니다(그 방향이 빈 것→비어 있지 않은 것).
`interrupt()`는 잠금 밖에서 호출합니다 — 순서를 거꾸로 하면, 잠금을 쥔 상태로
시스템 콜을 하게 되고, 이유가 없습니다. `start_write`도 같은 방식으로
처리합니다. 회귀 테스트 `tcp_multithread_wakeup_test`의 판별 방법은
직접적입니다: 한 스레드가 `WSAPoll`에서 잠들고, 다른 스레드가 읽기를 등록한 후
상대가 데이터를 쓴다 — handler가 3초 이내에 실행됨을 단언합니다. 깨움이
없으면, 유일한 깨움원은 5초 후의 워치독 타이머이고, 테스트는 반드시
실패합니다.

**두 번째 누락: 새 최초 타이머.** descriptor를 고친 후, 동족 문제가 하나 더
남아 있었습니다. 제4장의 수면 공식은 "다음 타이머 만료 - now"입니다 — 이
타임아웃도 **스냅샷**입니다: 계산한 후에 더 빠른 타이머가 다른 스레드에서
들어와도, 잠든 스레드는 여전히 오래된 시각에 따라 눈을 뜹니다. 더 나쁜 변체:
무한히 잠듦으로 계산된(당시 타이머 없음) 후, 다른 스레드가 `async_wait`로
100ms 타이머를 넣는다 — 아무도 깨우지 않으면, 이 잠듦에는 끝이 없습니다.

asio도 여기에 답을 가지고 있습니다. `select_reactor::schedule_timer`:

```cpp
bool earliest = queue.enqueue_timer(time, timer, op);
if (earliest)
  interrupter_.interrupt();     // new earliest timer → wake and recompute
```

sasio의 수리는 `timer_queue::add`가 "새 entry가 힙 꼭대기가 되었는가"를
반환하게 하고, `steady_timer::async_wait`가 이에 따라
`scheduler::wake_waiters()`를 호출합니다(내부는 `reactor_.interrupt()`).
회귀 테스트 `timer_multithread_wakeup_test`: run 스레드를 먼저 잠들게 하고
(타이머 없음), 메인 스레드가 100ms 대기를 넣습니다 — 수리 전에는 handler가
영원히 실행되지 않으며, 테스트는 `fired` 플래그로 실패합니다(1초 후 메인
스레드가 `stop()`으로 마무리합니다).

**3 개의 대조, 1 개의 패턴.** post의 깨움은 "상태를 바꾸는 곳에서, 그 자리에서
깨운다"입니다. 두 번의 누락은 모두 "새 대기원"의 두 입구(새 descriptor, 새
타이머)였고, 수리는 모두 "새로운 최초 대기자가 됨 → 깨운다"였습니다. 요약하면,
스냅샷 모델의 핵심 원칙은 1 문: **`WSAPoll`의 답을 바꿀 수 있는 모든 작업은, 그 답을
기다리는 사람을 반드시 깨워야 합니다**. asio의 `epoll_reactor`에는 첫 번째
누락이 없습니다 — epoll의 descriptor 집합은 커널 안에 있어서, `epoll_wait`가
새 등록을 자연히 봅니다. 스냅샷 문제는 애초에 존재하지 않습니다. 하지만
타이머의 누락은, timerfd 없는 epoll에서도 존재합니다(`update_timeout()` →
`interrupt()`). 플랫폼 기제는 다르지만, 스냅샷의 함정은 공유됩니다.

## 정독: run_for의 4 악장

깨움의 수신 측은 `run_for`입니다. 전문 약 100줄, 골격은 4 단:

```cpp
void reactor::run_for(clock::duration timeout)
{
  std::vector<WSAPOLLFD> poll_fds;        // ① copy the snapshot under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (each row of the descriptor table)
      if (read or write registered)
        { poll_fds.push_back(fd); remember socket; }
    poll_fds.push_back(interrupter);      // the truncheon rides last
  }                                       // ② lock released — poll without it

  const int result = ::WSAPoll(poll_fds.data(), ..., to_milliseconds(timeout));

  std::vector<std::function<void()>> ready_handlers;   // ③ move out under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (each ready descriptor)
      move that direction's callback out of the table into ready_handlers;
  }
  for (auto& handler : ready_handlers)    // ④ execute without the lock
    handler();

  if (interrupter readable) { drain buffer; clear flag; }  // ⑤ tidy up
}
```

4 개의 세부사항, 각각에 한 단락씩:

- **② poll 중에는 잠금이 없습니다**. `WSAPoll`은 무한히 잠들 수 있습니다.
  reactor의 잠금을 쥔 채로 잠들면, 다른 스레드의 모든 `start_read`가
  막힙니다 — 교착의 고전 조리법(잠금을 필요로 하는 사람이 당신을 깨우려 하고,
  당신은 잠금을 쥔 채 깨워지기를 기다립니다). 스냅샷은 잠금 안에서 복사가
  끝나고, poll은 잠금 없이 진행됩니다. 둘 사이의 시간창이 바로 "깨움이
  필요한" 이유입니다;
- **③ 꺼낸 후 실행**. 레벨 트리거 poll은 버퍼에 데이터가 있는지만 봅니다. 준비
  콜백이 표에 남아 있고 `recv`가 한 박자 늦게 실행되면, 다음 poll이 같은
  descriptor를 또 보고합니다. 콜백을 표에서 꺼내는 것(불변식 2)이 가짜
  경보와 이중 전달을 모두 막습니다 — `do_read`가 정말 `would_block`을 읽으면,
  자기가 다시 등록합니다;
- **④ 잠금 없이 실행**. scheduler의 "잠금 안에서 디큐, 잠금 밖에서 실행"과
  같은 철칙: 콜백은 재등록을 하고, post를 합니다 — 모두 잠금을 필요로 할 수
  있습니다;
- **⑤ 자기 몫만 배수**. 여러 스레드가 각자 자신의 `WSAPoll`에서 잠들어 있을
  때, 같은 1바이트가 모두를 깨울 수 있습니다. 먼저 깬 사람이 버퍼를 읽어
  없애고, 나머지 스레드는 `recv`가 비어 있는 것을 보고, 플래그를 지우고,
  각자의 루프로 돌아갑니다. 몇 번의 헛깨움은 무해합니다 — 큐 자체가 진실의
  원천이며, 깨움은 초인종이지 배달원이 아닙니다.

`WSAPoll`이 오류를 반환할 때의 처리에도 요령이 있습니다: `WSAEINTR`은 바로
반환합니다(중단된 시스템 콜은 실패가 아님), 나머지 오류는 `std::system_error`를
던집니다 — 교육용 라이브러리는 오류를 삼키지 않습니다.

## 대조표

| 질문 | asio(select 백엔드) | asio(epoll 백엔드) | sasio |
| --- | --- | --- | --- |
| 대기 시스템 콜 | `select()`(IOCP 없는 Windows에서는 WSAPoll) | `epoll_wait` | `WSAPoll` |
| descriptor 집합 | 사용자 공간 스냅샷 | 커널 측, 등록 즉시 유효 | 사용자 공간 스냅샷 |
| 새 등록 시 깨움 | 필요(`start_unready_op`) | 불필요(epoll이 자연히 보임) | 필요(`if (first)`) |
| 새 최초 타이머 시 깨움 | 필요(`schedule_timer`) | timerfd면 불필요 / 아니면 필요(`update_timeout`) | 필요(`add`가 earliest 반환) |
| interrupter | socket pair / eventfd | eventfd / socket pair | 자기 연결 UDP socket |
| 소스 입구 | `detail/impl/select_reactor.ipp` | `detail/impl/epoll_reactor.ipp` | `src/reactor.cpp` |

한 문장: **스냅샷 모델의 모든 복잡성은 "깨어 있는 상태를 유지하는 것"에
쓰입니다** — epoll은 descriptor 집합을 커널에 두어, 정확히 이 부분을
아꼈습니다. select/WSAPoll 경로의 깨움 로직의 단계 단계는, asio와 sasio가
동형입니다.

## 연습

**실험**: 첫 번째 누락을 직접 재현합니다. `src/reactor.cpp`를 열고,
`start_read`와 `start_write`의 2곳의 `if (first) interrupt();`를 주석 처리하고,
재빌드하여 `test_ip_tcp.exe`를 단독 실행합니다:

```text
cmake --build build --target test_ip_tcp
build\test_ip_tcp.exe
```

`tcp_multithread_wakeup_test`의 결말을 보십시오: 약 5초 후에 실패 — handler가
달리지 않은 것이 아니라, 5초 늦은 것입니다(워치독 타이머가 깨운 것).
2줄을 복원하면, 테스트는 즉시 통과합니다. 방금 검증한 것은 "스냅샷 모델의
대가"입니다: 깨움 한 줄의 결핍은, 밀리초 수준의 전달을 "다음 우연한 이벤트를
기다림"으로 퇴화시킵니다.

**버그 사냥**: `src/ip_tcp.cpp`의 `do_read()`는, `recv`가 `would_block`을
반환할 때, 오류를 보고하는 대신 `reactor_.start_read(...)`를 호출하여 자신을
다시 등록합니다. 먼저 답하세요: poll이 "읽을 수 있다"고 했는데, `recv`가
어떻게 못 읽을 수 있습니까? 그다음: 재등록하지 않으면, 이 읽기 연산은 어떤
상태가 됩니까? (힌트: 콜백은 이미 표에서 꺼내졌습니다 — 불변식 2.)

---

준비, 깨움, descriptor 테이블이 모두 갖춰졌습니다. 세 가지 깨움원 중 하나가
남았습니다: 시간. 제4장의 루프는 "다음 타이머의 만료"로 잠들 시간을 정합니다 —
그 "다음"은 어디에서 오는가. 최소 힙의 유지, 취소가 결코 재진입하지 않는 이유,
그리고 asio 자가 테스트가 잡은 힙 범위 외 접근의 버그. 다음 장: timer_queue.
