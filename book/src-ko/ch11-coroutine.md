<!-- translated-from: book/src/ch11-coroutine.md sha256:67c19c8bed8d -->

# coroutine: 콜백을 직선 코드로

제8장의 `write_op`은 shared_ptr 릴레이로 1단의 콜백을 체인에 접었지만, 실제
프로토콜 코드는 더 많습니다 — "메시지 헤더를 읽는다, 그다음 본문을 읽는다,
해석하고, 되돌려 쓰고, 반복한다", 네다섯 단계의 비동기 단계가 이어지면,
콜백식에서는 이렇게 자랍니다:

```cpp
void do_read_header(std::shared_ptr<session> self)
{
  async_read(self->socket_, header_buf,
      [self](error_code ec, std::size_t) {
        if (ec) { close(self); return; }
        self->parse_header();
        async_read(self->socket_, body_buf,
            [self](error_code ec, std::size_t) {
              if (ec) { close(self); return; }
              // ...each step deeper, each piece of state further away
            });
      });
}
```

제어 흐름이 중첩된 lambda로 분쇄됩니다: 공유 상태는 `self` 캡처에 의존하고,
오류 처리는 층마다 복사되며, 조기 탈출의 경로를 수동으로 추적해야 합니다.
C++20 coroutine은 같은 로직을 직선으로 쓸 수 있게 합니다:

```cpp
sasio::awaitable<void> session(tcp::socket& socket)
{
  char header[16];
  co_await sasio::async_read_some(socket, sasio::buffer(header));
  // parse_header() sits right here
  char body[64];
  co_await sasio::async_read_some(socket, sasio::buffer(body));
  // ...
}
```

이 장은 `include/sasio/awaitable.hpp`와 `co_spawn.hpp`를 열어 3가지를
답합니다: `co_await`가 서스펜드할 때 무엇이 일어나는가? coroutine이 끝날 때,
완료 통보는 어디에서 오는가? 그리고 왜 sasio의 coroutine이 asio의
completion token 기계를 필요로 하지 않는가?

## 설계: 태스크 타입과 그 약속

`awaitable<T>`는 **coroutine 반환 타입**입니다 — 함수가 그것을 반환한다고
선언하면, 본문에서 `co_await` / `co_return`을 사용할 수 있습니다. 컴파일러는
함수 본문을 큰 상태 기계로 재작성하고, 그 기장 규칙을 `promise_type`이
제공합니다. sasio의 promise에는 5개의 멤버가 있으며, 각각이 coroutine 생애의
한 가지 질문에 답합니다:

```cpp
struct promise_type
{
  std::optional<T> value;        // co_return lands here
  std::exception_ptr error;      // unhandled_exception lands here
  std::function<void(std::exception_ptr, T)> completion;  // top level: who gets the result
  std::coroutine_handle<> continuation;                   // nested: who gets control back
  bool top_level = false;        // set by co_spawn; decides the ending
};
```

불변식 3개:

1. **awaitable은 coroutine 프레임을 배타적으로 소유합니다**. move-only입니다
   (복사 삭제), `co_await`는 rvalue만 받아들입니다 — 이름이 붙은 변수에 대한
   `co_await task`는 컴파일되지 않으며, `co_await std::move(task)`라고
   씁니다. 자랑이 아닙니다: 프레임의 파괴권을 소유하는 자는 정확히 한 명이어야
   하며, 2개의 코드 경로가 모두 자신이 그것을 보유하고 있다고 믿으면, 그것은
   double free입니다;
2. **톱 레벨과 중첩의 마무리가 다릅니다**. `co_spawn`이 시작한 톱 레벨
   coroutine은 `final_suspend`에서 **서스펜드하지 않습니다** — 프레임은 그
   자리에서 파괴되며, 완료 handler는 promise의 소멸자 안에서 호출됩니다;
   `co_await`된 중첩 coroutine은 `final_suspend`에서 서스펜드하고, symmetric
   transfer로 제어권을 대기자에게 돌려주며, 프레임은 대기자가 해방합니다;
3. **비동기 완료는 run 루프로 돌아가고, 동기 완료는 발기 스택으로
   돌아갑니다**. 누가 coroutine을 resume했는가, 그 스레드의 스택 위에 완료
   통보가 있습니다: 서스펜드한 적이 있는 coroutine은 engine의 완료 전달로
   재개됩니다(`run()` 안); 한 번도 서스펜드하지 않고 직접 `co_return`으로
   끝난 coroutine은, `co_spawn`의 첫 번째 `resume()` 안에서 프레임이 파괴됩니다
   — 완료 handler는 **co_spawn의 호출 스택** 위에서 실행되며, `run()` 밖일 수
   있습니다. 자신의 handler가 어느 스택 위에서 달리는지, 쓰는 시점에
   의식하세요.

## 한 coroutine의 생애

테스트의 실제 사례를 추적합니다:

```cpp
sasio::awaitable<int> return_after_wait(sasio::steady_timer& timer)
{
  co_await sasio::async_wait(timer);
  co_return 42;
}

// in the test:
co_spawn(ioc, return_after_wait(timer),
    [&](std::exception_ptr e, int result) { value = result; });
ioc.run();
```

`co_spawn`부터 `42`가 착지할 때까지, 8단계:

```text
① co_spawn: take the handle, set top_level=true, store completion, resume()
② the body runs → co_await async_wait(timer)
③ async_wait builds async_operation<void>, co_awaits it
    → await_suspend: remember awaiting_ (this coroutine's handle), call start(...)
    → start calls timer.async_wait(handler), registering in the heap
    → async_operation::await_suspend returns → the coroutine suspends,
      control returns to co_spawn
④ co_spawn returns (the coroutine is asleep on the timer)
⑤ the timer expires → the run loop invokes the timer's completion handler
    → the handler calls awaiting_.resume(): the coroutine continues at co_await
⑥ async_operation::await_resume: error_ empty → normal return
⑦ co_return 42 → promise::return_value(42) → final_suspend()
    → final_awaiter::await_ready() == true (top_level) → no suspend
    → the frame destroys on the spot → ~promise_type calls completion(no error, 42)
⑧ the test's lambda receives 42
```

단계 ③에서 한 번 멈출 가치가 있습니다: **coroutine의 서스펜드 지점은 "1개의
비동기 연산을 등록하는 것"입니다**. `async_operation`이 콜백식의 발기 함수
(`timer.async_wait(...)`)를 co_await 가능한 객체로 감쌉니다 — 발기, 등록,
서스펜드가 한 호흡으로 행해집니다. 콜백식과 coroutine식을 잇는 다리는 이
하나뿐이며, 다리의 양안은 같은 엔진입니다.

## 정독: final_awaiter와 소멸자에 숨은 완료

coroutine의 마무리는 구현에서 가장 꼬인 부분입니다. sasio는 20줄로 압축했습니다:

```cpp
struct final_awaiter
{
  promise_type* self;

  // Top-level coroutines (started by co_spawn) do not suspend at the end:
  // the frame is destroyed automatically and ~promise_type runs the
  // completion handler. Awaited coroutines do suspend and hand control to
  // their awaiting coroutine via symmetric transfer.
  bool await_ready() const noexcept { return self->top_level; }

  std::coroutine_handle<> await_suspend(
      std::coroutine_handle<promise_type> handle) noexcept
  {
    std::coroutine_handle<> next = handle.promise().continuation;
    return next ? next : std::noop_coroutine();
  }

  void await_resume() noexcept {}
};
```

`co_return` 후, 컴파일러는 `final_suspend()`를 호출하고 이 awaiter를
받습니다:

- **톱 레벨**(`top_level == true`): `await_ready`가 true를 반환하며,
  서스펜드를 건너뜁니다 — 본체가 끝나고 프레임은 **그 자리에서 파괴**됩니다.
  `~promise_type`은 프레임 파괴의 마지막 경유지이며, 완료 통보는 거기서
  나갑니다:

  ```cpp
  ~promise_type()
  {
    if (completion)
    {
      try
      {
        completion(error, value ? std::move(*value) : T{});
      }
      catch (...) {}
    }
  }
  ```

  예외와 반환값이 한 번의 호출에 합류합니다. completion 안에서 던지는 것은
  모두 삼켜집니다 — "결과의 인도"는 coroutine의 마지막 행동이며, 그 후의 throw에는
  받을 사람이 없습니다. 완료 handler가 **인라인으로** 호출되는 것에 주목:
  그 스레드는 coroutine을 마지막으로 resume한 스레드입니다 — 그 resume은 engine의
  완료 전달에서 온 것(단계 ⑤)이므로, 계약 2는 지켜집니다.

- **중첩**: `await_ready`가 false를 반환하고 `await_suspend`에 들어갑니다 —
  반환값은 **다음에 재개할 coroutine 핸들**(대기자)이며, 대기자가 없으면
  `std::noop_coroutine()`을 반환합니다. 컴파일러는 그 핸들로 직접
  점프하여 재개하고, **호출 스택의 층을 추가하지 않습니다** — 이것이 symmetric
  transfer입니다. 이것이 없으면, 깊이 중첩된 coroutine 체인은 반환할 때마다
  스택을 1단씩 쌓고, 긴 체인에서 오버플로합니다. 있으면, 제어권이 릴레이
  배턴처럼 이동합니다.

톱 레벨이 "그 자리에서 파괴"하고, 누군가 수집하기 위해 서스펜드하지 않는
이유는? 톱 레벨에는 대기자가 없습니다 — completion 외에, 그 존재를 아는
사람은 없습니다. 아무도 다시 resume하러 오지 않는 coroutine을 서스펜드시켜 두는
것은, 프레임을 1개 누출하는 것입니다. "서스펜드하지 않음 → 파괴 → 소멸자가
결과를 인도"로, 누출의 구멍을 용접으로 막았습니다.

## 대조표

| 질문 | asio | sasio |
| --- | --- | --- |
| 태스크 타입 | `awaitable<T, Executor>`(executor 있음) | `awaitable<T>`(executor 없음) |
| 프레임 기장 | `impl/awaitable.hpp`의 `awaitable_frame_base`(`awaitable_launch_context` 상속, 사용자 정의 `operator new`가 프레임을 리사이클) | `promise_type` + 핸들 소유권 |
| 시작 | `co_spawn`(executor 경유로 전달) | `co_spawn`(직접 `resume()`) |
| 완료 통보 | `async_result` 특수화 + token(`use_awaitable`) | `promise_type::completion` |
| 서스펜드/재개 | `awaitable_frame::resume()` | `awaiting_.resume()` |
| 소스 입구 | `awaitable.hpp`, `impl/awaitable.hpp`, `impl/co_spawn.hpp` | `include/sasio/awaitable.hpp`, `co_spawn.hpp` |

규모 격차의 뿌리는 4행입니다. asio의 `co_spawn(ctx, task, token)`의 token은
`use_awaitable`, `use_future`, detached...를 받아들입니다 — "결과를 어떻게
돌려주는가"는 token이 결정하므로, 완료는 `async_result` 특수화 체계를
통과해야 합니다(제13장 주제). sasio는 token을 잘라내고, 완료를 promise의
소멸자에 직접 썼습니다 — 한 문장으로 끝나는 이야기입니다. 대가도 대장에
있습니다: `use_future` 없음, detach 없음, 입구마다 자유 함수 어댑터 1개
(`co_spawn.hpp`의 `async_wait` / `async_read_some` 등).

또 하나의 규모 차이: asio의 `awaitable_frame_base`는
`awaitable_launch_context`를 상속하고, `operator new`를 사용자 정의하여
**프레임 메모리의 재활용**을 하고 있습니다. co_spawn의 work 카운트는
`co_spawn_state`의 work guard(`impl/co_spawn.hpp`)에 걸려 있고, 프레임 안에는
없습니다. sasio의 coroutine 프레임은 "벌거벗은" 것입니다: 메모리는 컴파일러의
coroutine 기계에 맡기고, work는 **하위 연산**(타이머나 socket의 등록)에
의존합니다. 효과는 같습니다(coroutine이 기다리는 동안 `run()`은 빠져나가지
않습니다)만, 기제가 다릅니다 — asio는 프레임을 리소스 풀로 관리하고, sasio는
프레임을 보통 객체로 취급합니다.

## 연습

**실험**: **재귀적인** coroutine 체인을 작성하여, symmetric transfer가 스택을
먹지 않음을 검증합니다. 주의: "순서대로 천 번 await하는" 것은 중첩이 아닙니다
— 어느 순간에도 기다리는 것은 1단이며, 스택 깊이와는 무관합니다. 진짜 깊은
체인을 만들려면, coroutine이 coroutine을 await해야 합니다:

```cpp
sasio::awaitable<int> chain(sasio::steady_timer& t, int depth)
{
  if (depth == 0)
  {
    co_await sasio::async_wait(t);            // the only suspension, at the bottom
    co_return 0;
  }
  co_return co_await chain(t, depth - 1);     // recursion: 1000 frames alive at once
}

// co_spawn(ioc, chain(timer, 1000), [&](std::exception_ptr e, int r) { ... });
```

`chain(1000)`은 1000단의 coroutine 프레임을 동시에 유지합니다(각 층이 다음 층을
기다림); 1000개의 서스펜드 지점 중 **가장 깊은 하나만이 타이머**입니다 —
나머지 999개는 "자식 coroutine"을 기다립니다. 타이머가 발화하면, 완료는 1000단의
`co_return`을 따라 한 층씩 위로 전달됩니다 — 모든 홉이 symmetric transfer이며,
디버거로 호출 스택을 보면, 깊이가 층수와 함께 증가하지 않습니다(보통의 재귀
함수와 비교: 1000회 호출, 1000개 스택 프레임). 통과한 후, `chain`의
`final_suspend`를 `std::suspend_always{}`로 바꾸고(서스펜드해도 아무도
재개하지 않음) 다시 실행합니다 — 관찰되는 현상: 체인이 어느 층에서 얼어
붙고, `run()`은 타이머 체인이 끊어져서 조기에 정지합니다. 불변식 2의 부정적
교재 그 자체: **아무도 수집하지 않는 프레임은 누출입니다**. asio의
awaitable_frame은 프레임 재활용 풀로 이것을 받쳐 주고, sasio는 "톱 레벨은
반드시 파괴, 중첩은 반드시 await된다"는 약속으로 받쳐 줍니다.

**버그 사냥**: 이 코드는 `co_await` 줄에서 컴파일에 실패합니다:

```cpp
auto task = return_after_wait(timer);
co_await task;        // ← compile error
```

2가지 질문: 오류 메시지의 어느 단어가 원인을 가리키는가? 올바른 철자는
무엇인가? 그리고 더 깊이 — 왜 복사를 허용하는 것보다 move-only가 더 안전한가?
힌트: 하나의 awaitable의 2개 복사본이 모두 co_spawn되면, 누가 coroutine
프레임을 파괴할 책임을 집니까?

---

콜백식과 coroutine식이 모두 완성되었습니다. 하지만 제12장까지, 이 책은 계속
"잘려나간 합성 계층"을 언급해 왔습니다: executor와 completion token.
169,063줄 중 수만 줄에, 그것들은 어째서 값을 하는가? 다음 장은 executor를
엽니다.
