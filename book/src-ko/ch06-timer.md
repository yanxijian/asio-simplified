<!-- translated-from: book/src/ch06-timer.md sha256:2d8e5dfc5395 -->

# 시간: timer_queue와 steady_timer

run 루프는 타이머 큐에 두 가지를 묻습니다: "다음 것은 언제 만료되는가"(얼마나
잘지), "지금 만료된 것이 있는가"(일할지 말지). 제5장의 깨움 경로에는 세 번째가
추가되었습니다: "새 타이머가 가장 이른 것인가"(깨울지 말지). 세 가지 모두
`timer_queue`가 답하며, 데이터 구조는 교과서적인 최소 힙입니다 — 그런데 이
140줄도 안 되는 파일에는, asio 자체 테스트 스위트가 잡은 이 책에서 유일한
진짜 버그가 묻혀 있습니다. 이 장에서 해부합니다.

## 설계: 힙, 인덱스, 생존 기간

대기 중인 타이머는 `timer_queue` 안에서 하나의 `entry`입니다:

```cpp
struct entry
{
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  clock::time_point when;                    // expiry moment
  std::function<void(error_code)> handler;   // who to call
  std::size_t heap_index = npos;             // my position in the heap
};
```

힙 본체는 `std::vector<std::shared_ptr<entry>>`이며, `when`으로 정렬된 최소
힙입니다. 불변식 2개:

1. **힙 순서가 성립하고, 힙 안의 모든 entry의 `heap_index`가 실제 위치와
   일치합니다**; 힙에 없는 entry의 `heap_index`는 반드시 `npos`여야 합니다.
   두 부분 모두 빠지면 안 됩니다 — 전자는 `front()`가 항상 가장 이른 만료를
   보장하고, 후자는 "취소"가 낡은 인덱스로 힙을 함부로 만지지 않도록
   보장합니다;
2. **취소된 완료는 절대 제자리에서 호출되지 않습니다**. `cancel()`이 어느
   호출 스택에서든(다른 handler의 스택 안에서도) 호출되어도, 취소된 handler는
   `post`를 통해서만 전달됩니다 — 재진입은 비동기 코드에서 가장 음습한 지뢰이며,
   이 불변식이 그것을 제거합니다.

`entry`는 `shared_ptr`로 유지됩니다. 왜냐하면 **동시에 두 세계에 살아 있기
때문**입니다: 힙 안에 하나의 참조, 타이머 객체(`steady_timer`)의 `pending_`에
하나의 참조. 먼저 움직이는 자가 운명을 결정합니다 — `cancel()`이 먼저 잠금을
잡으면 handler를 끌어내어 취소를 전달하고, `take_expired`가 먼저 잡으면
handler를 끌어내어 성공을 전달합니다. 상호 배타 잠금이 이 경쟁의 창을 원자까지
좁히며, 어느 쪽도 "반쯤 죽은" 상태를 보지 않습니다.

## 정독: pop_at과 진짜 버그

힙의 핵심 작업은 "임의의 위치에서 요소를 삭제하는" 것입니다. 힙 꼭대기의 삭제
(`take_expired`가 `pop_at(0)`을 호출)와 취소(`remove`가 `pop_at(임의위치)`를
호출)는 같은 코드를 공유합니다:

```cpp
void timer_queue::pop_at(std::size_t index)
{
  if (index >= heap_.size())
    return;

  const std::shared_ptr<entry> removed = heap_[index];
  const std::size_t last = heap_.size() - 1;

  // Move the last element into the hole. Do not use swap_at() here: it would
  // write the removed entry's heap_index back to a valid-looking value, which
  // is exactly the stale index that made a later remove() index out of bounds.
  if (index != last)
  {
    heap_[index] = std::move(heap_[last]);
    heap_[index]->heap_index = index;
  }

  heap_.pop_back();
  removed->heap_index = entry::npos;      // ← the fix: leaving the heap is npos

  if (index < heap_.size())
  {
    sift_down(index);
    sift_up(index);
  }
}
```

이 주석의 "Do not use swap_at() here"가 이 장의 핵심입니다. 힙의 교과서적인
삭제 방법은: 마지막 요소를 구멍 위치로 옮기고, `pop_back`한 후, 새 위치에서
위아래로 한 번씩 체질합니다. 게으른 지름길은 기존의 `swap_at`을 재활용하는
것입니다:

```cpp
// the buggy version:
swap_at(index, last);    // swap_at rewrites BOTH entries' heap_index!
heap_.pop_back();
```

`swap_at`의 직책은 힙 순서의 유지이며, 충실하게 **양쪽** 요소의 인덱스를
갱신합니다 — 삭제되려고 하는 entry도 포함해서. 삭제 후,
`removed->heap_index`는 더 이상 합법적이지 않은 위치를 가리킵니다. 구체적인
숫자로: 힙에 2개의 타이머, 정점을 취소. `pop_at(0)`: `index=0`, `last=1`,
교환 후 삭제된 entry의 `heap_index`는 1이 됩니다; `pop_back` 후 힙의 크기는
1로 줄어듭니다. 이 시점에 `removed->heap_index == 1 == heap_.size()` —
범위 외의 값이 확정됩니다.

언제 발생하는가? 이 entry는 `steady_timer`의 `pending_`가 참조를 가지며,
타이머 객체의 소멸자가 `cancel()`을 호출하고, `cancel`이 `remove`를 호출하며,
`remove`는 `heap_index = 1`로 크기 1의 힙을 조회합니다 — `heap_[1]`은 범위
외이고, Debug판 STL의 단언이 그 자리에서 튀어나옵니다
(`vector subscript out of range`).

왜 이 버그가 살아남을 수 있었는가? **2개**의 타이머가 필요하기 때문입니다:
타이머가 하나만 있다면 `index == last`, 교환 없음, 범위 외 없음, 힙을
비우는 것도 아무 일 없습니다. 최초의 자작 테스트는 우연히 타이머를 하나만
사용했습니다. asio 자가의 `deadline_timer` 테스트를 이식할 때 — 테스트
케이스가 2개의 타이머를 나란히 놓았을 — Debug 단언이 한 방에 튀어나왔습니다.
이것이 전체의 "테스트 이식이 진짜 버그를 잡은"의 유래입니다. 수리는 2 단계로
나뉩니다: `pop_at`은 더 이상 `swap_at`을 사용하지 않고, 힙을 떠나면 `npos`를
설정합니다(근본 원인); `remove`의 시작에서 인덱스에 **경계 + 신원** 이중
검사 —

```cpp
const std::size_t index = e->heap_index;
if (index == entry::npos || index >= heap_.size() || heap_[index] != e)
{
  e->heap_index = entry::npos;
  return false;
}
```

설령 미래에 또 더러운 인덱스가 나타나더라도, `remove`는 조용히 "힙에 없다"고
반환할 뿐, 범위 외에 도달하지 않습니다. 근본 원인의 수리와 방어적인 검증이,
2 단계로 각각 자신의 임무를 수행합니다.

같은 `add`는 제5장의 깨움 신호도 겸합니다 — "새 entry가 가장 이른 것이
되었는가"를 반환합니다:

```cpp
const bool earliest = heap_.empty() || e->when < heap_.front()->when;
// ...push, sift up...
return earliest;      // steady_timer decides on wake_waiters() from this
```

1개의 반환값이 "스냅샷 모델의 2번째 실패"의 수리를 힙 내부에서 깨끗하게
인출합니다 — 힙 자신은 reactor의 존재를 모르며, 잠금 순서
(scheduler → timer → reactor)도 파괴되지 않습니다.

## steady_timer: 대기, 취소, 소멸

힙은 엔진 부품이고, `steady_timer`는 사용자가 손에 넣는 핸들입니다. 3개의
작업을 하나씩 봅니다.

**`async_wait`**: `{expiry_, handler}`를 entry로 포장해 힙에 겁니다. 하나의
타이머 객체가 동시에 허용하는 대기는 하나입니다 — 두 번째 `async_wait`는 먼저
이전 것을 `cancel`합니다(오래된 대기는 `operation_aborted`를 가지고 완료되고,
새로운 대기가 힙에 들어갑니다). asio는 같은 객체에 임의 수의 대기를 허용하며,
이것이 sasio의 차이표에 실린 간략화입니다.

**`cancel`**: `pending_`의 참조를 가져와서(`std::move`), 힙에 삭제를 요구합니다.
삭제가 성공하면 대기가 아직 만료되지 않은 것입니다 — handler를 entry에서
끌어내어, `post`로 `operation_aborted`를 가진 완료를 던집니다:

```cpp
// Never invoke the handler from inside cancel(): post it, exactly like asio.
scheduler_.post([handler = std::move(handler)]() mutable {
  handler(operation_aborted);
});
```

삭제가 실패하면 대기가 **이미 만료되었고, 완료가 이미 진행 중**임을 의미합니다
(scheduler의 `take_expired`가 handler를 가져갔습니다) — `cancel`은 0을 반환하고
아무것도 하지 않습니다. 이 판정의 잠금 규율에 주목하세요: `take_expired`가
handler를 entry에서 옮겨내는 것과 `cancel`의 `remove`는, 같은
`timer_queue::mutex_`로 직렬화되므로, "양쪽 다 handler를 얻는" 또는 "양쪽 다
얻지 못하는" 중간 상태는 존재하지 않습니다. 불변식 2가 여기서 실현됩니다:
`cancel`이 어느 스택에서 발기되든, handler는 `post`를 통해 run 루프 안에서
실행되며, 절대 그 자리에서 재진입하지 않습니다.

**소멸**: `~steady_timer`는 `cancel()`입니다 — 객체가 죽으면, 보류 중인 대기가
`operation_aborted`를 가지고 완료됩니다. asio의 의미론도 같습니다. 대가는,
"아직 대기 중"인 타이머의 소멸이 완료 handler를 큐에 주입하는 것이므로,
**소멸의 타이밍**을 마음에 두어야 합니다(그래서 co_spawn의 어댑터는 타이머를
참조로 가집니다 — coroutine이 살아 있는 한, 타이머도 살아 있어야 합니다).

## 대조표

| 질문 | asio | sasio |
| --- | --- | --- |
| 힙 본체 | `detail/timer_queue.hpp`(시계 유형별로 하나) | `src/timer_queue.cpp`(steady_clock만) |
| 타이머 신원 | `per_timer_data`(타이머마다 대기 연결 리스트) | `shared_ptr<entry>`(객체당 최대 1개 대기) |
| 다중 대기 | 지원; `expires_at`이 전부 취소 | 미지원; 새 대기가 오래된 대기를 자동 취소 |
| 취소 입도 | `cancel` / `cancel_one` | `cancel` |
| 완료 경로 | 완료 op가 scheduler 큐로 | 만료는 scheduler 루프에서 직접 호출; 취소는 `post` 경유 |
| reactor와의 관계 | 타이머 큐가 reactor에 등록되고, reactor가 poll 타임아웃 계산 | 타이머 큐는 독립된 service, scheduler가 poll 타임아웃 계산 |

2개의 차이를 더 설명합니다. 첫째, asio는 timer queue를 reactor에 걸고 reactor가
poll 타임아웃을 계산하며, sasio는 scheduler에 걸고 scheduler가 계산합니다 —
위치가 다르지만, "타임아웃은 스냅샷이고, 새 최초 타이머는 깨워야 한다"는
교훈은 양쪽에서 배웠습니다(asio의 `schedule_timer` interrupt, sasio의
`wake_waiters`). 둘째, 다중 대기의 부재는 게으름이 아닙니다: 그 구현 가치는
"하나의 타이머 객체를 여러 대기의 공용 핸들로"에 있고, sasio의 coroutine 어댑터
(제11장)는 절대 그렇게 사용하지 않습니다 — 각 co_await에 독립된 타이머로,
단일 대기면 충분합니다.

## 연습

**실험**: `pop_at`의 범위 외 버그를 직접 재현합니다. `src/timer_queue.cpp`를
열고, `pop_at`을 주석이 묘사하는 구현으로 되돌립니다(`swap_at(index, last)`을
사용하고, `removed->heap_index = entry::npos;` 줄을 삭제). 그리고 10줄의
이중 타이머 케이스를 씁니다:

```cpp
sasio::io_context ctx;
sasio::steady_timer t1(ctx), t2(ctx);
t1.expires_after(std::chrono::milliseconds(50));
t1.async_wait([](sasio::error_code) {});
t2.expires_after(std::chrono::milliseconds(300));
t2.async_wait([](sasio::error_code) {});
// t1 expires → pop_at(0) takes the swap path → the other entry's index goes dirty
// t1/t2 destruct → cancel → remove reads the dirty index → Debug assert fires
```

Debug판으로 빌드·실행하고, 단언이 튀어나오는 **시기와 호출 스택**을
보십시오 — 발생 위치는 `pop_at`이 아니라, **그 후**의 어딘가의 `remove`의
`heap_[index]`입니다. 올바른 구현으로 되돌리면, 단언은 사라집니다. 이 연습이
주는 것은 감각입니다: "인덱스와 위치가 항상 일치해야 하는" 구조에서, 더러운
인덱스의 발생 위치은, 항상 오류 발생 지점에서 멀리 떨어져 있습니다.

**버그 사냥**: `steady_timer::cancel`에서, `timers_.remove(entry)`가 false를
반환하면 함수는 직접 `return 0`합니다 — 아무것도 전달하지 않습니다. 그러나 이
시점에 entry의 handler는 아직 entry 안에 있고, entry는 `pending_`와 함께
곧 버려질 수 있습니다. handler는 사라져서, 영원히 호출되지 않는 것일까요?
(힌트: `remove`가 실패하는 이유는 3가지뿐 — 인덱스가 npos, 범위 외, 신원
불일치. 하나씩 생각합니다: 각각의 경우, handler의 완료는 누가 책임을 집니까?
답은 `take_expired`가 잠금을 잡는 타이밍 안에 있습니다.)

---

큐, 준비 완료, 시간 — 엔진의 3대 부품이 모두 분해 완료되었고, 깨움 경로의
수리도 모두 소정의 위치에 있습니다. 다음 장부터, 이 엔진으로 정말 유용한 것을
합니다: `socket`의 `async_read_some`은 발기 시 "상태를 저장, 관심을 등록"하고,
준비가 되면 `do_read`가 시스템 콜에 손을 댑니다 — 발기 함수와 완료 경로의
2상식은, asio의 모든 socket 코드의 공통 형태입니다.
