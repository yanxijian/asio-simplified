<!-- translated-from: book/src/ch13-tokens.md sha256:1c2087b7f933 -->

# completion token 계층: 결과를 돌려받는 방식

executor 계층이 "어디서 달리는가"에 답했으며, 이 장 — 이 계층의 다른 절반 — 은
**결과를 어떤 형식으로 호출자에게 돌려주는가**에 답합니다. 같은 발기 함수,

```cpp
socket.async_read_some(buffer, /* what goes here? */);
```

에 대해, 호출자마다 원하는 것이 완전히 다릅니다: 콜백을 원하는 사람,
`std::future`를 원하는 사람, coroutine을 원하는 사람, 결과에 관심이 없는 사람
(발사하고 잊기). asio의 대답은 "여기에 무엇을 넣을까"를 컴파일 시간 매개변수로
만드는 것 — **completion token**이며, 특수화 메커니즘으로 같은 발기 함수가
결과 형식마다 정확히 1번만 구현되도록 합니다.

## 설계: token은 컴파일 시간의 정책

기제를 3가지 규칙으로 압축합니다:

1. **모든 발기 함수가 자신의 완료 시그니처를 선언합니다**. `async_read_some`의
   시그니처는 `void(error_code, std::size_t)`입니다; 발기 함수는 token 자체를
   처리하지 않습니다 — 시그니처와 token을 함께 `async_result`에 넘길 뿐입니다;
2. **`async_result<Token, Signature>`의 특수화가 2가지를 결정합니다**: 발기
   함수의 반환 타입, 그리고 "엔진에 실제로 넘겨지는 완료 handler의 모양".
   기본 특수화(token이 일반 콜백인 경우)는 `void`를 반환하며 handler는 token
   그 자체입니다 — 여러분이 20년간 써온 콜백 코드는 모두 이 기본 특수화를
   통과했습니다;
3. **사용자가 자신의 token을 정의할 수 있습니다**. 새로운 타입에 대해
   `async_result`를 특수화하면, 그것은 이 라이브러리의 일급 시민이 됩니다 —
   `use_future`, `use_awaitable`, `deferred`, `detached`는 모두 이 기제의
   **사용자**이지, 엔진의 하드코딩이 아닙니다.

sasio에는 이 기제가 없습니다. 콜백이 유일한 형식이며, coroutine은 `co_spawn.hpp`의
명시적 어댑터에 의존합니다. 이 장은 asio의 특수화 체계를 열고, "sasio에
장착한다면 최소로 무엇인가"에 답합니다.

## trace: 4개의 token, 4개의 경로

같은 발기 호출, 4개의 token, 4개의 경로:

```text
callback:   async_read_some(buf, my_lambda)
              → the async_result default specialization
              → completion_handler = my_lambda
              → initiate → complete → my_lambda(ec, n) runs in run()

detached:   async_read_some(buf, asio::detached)
              → the detached specialization → the completion handler is
                a no-op object → complete → nothing happens
              (errors swallowed too — use with care)

use_future: async_read_some(buf, asio::use_future)
              → the use_future specialization → the return value becomes
                std::future<size_t>
              → the completion handler assigns the promise → you .get() elsewhere

use_awaitable: co_spawn(ctx, session(), asio::detached)
              → every co_await inside the coroutine resumes through the
                awaitable frame
              → the two ends of the same road as sasio's async_operation
```

제2장의 trace가 "발기 함수는 무엇을 반환하고, handler는 누구인가"를 물었습니다
— 이제 정확히 말할 수 있습니다: **그 둘 다 `async_result` 특수화가 결정합니다**.
발기 함수의 형태는 고정된 골격(`async_initiate`)으로 축소되고, 가변적인 것은
token뿐입니다.

## 정독: async_result의 주 템플릿

`asio/async_result.hpp`의 주 템플릿(문서 주석 제거):

```cpp
template <typename CompletionToken,
    ASIO_COMPLETION_SIGNATURE... Signatures>
class async_result :
  public conditional_t<
      detail::are_any_lvalue_completion_signatures<Signatures...>::value
        || !detail::are_any_rvalue_completion_signatures<Signatures...>::value,
      detail::completion_handler_async_result<CompletionToken, Signatures...>,
      async_result<CompletionToken,
        typename detail::simple_completion_signature<Signatures>::type...>
    >
{
public:
  typedef CompletionToken completion_handler_type;

  typedef void return_type;

  explicit async_result(completion_handler_type& h);

  return_type get();

  template <typename Initiation, typename RawCompletionToken, typename... Args>
  static return_type initiate(
      Initiation&& initiation,
      RawCompletionToken&& token,
      Args&&... args);
};
```

주 템플릿이 기술하는 것은 **기본 행동**입니다(token이 곧 handler, void 반환).
각 token 타입은 자신의 특수화를 제공하며, `return_type`과
`completion_handler_type`을 덮어씁니다. `initiate`가 핵심 설계입니다 — 발기
함수를 2개로 나눕니다:

- **token 처리**（특수화의 일）: use_future의 특수화는 promise를 만들고,
  "promise에 값을 대입하는" 객체를 완료 handler로 하며, `future`를 반환값으로
  돌려줍니다;
- **실제 발기**（`Initiation`의 일）: 처리된 완료 handler를 가지고 socket의
  `async_read_some` 구현을 실제로 호출하고, 엔진에 등록합니다.

나누는 것으로, 발기 함수의 저자는 `Initiation`을 1회만 쓰면 됩니다; token
체계가 어떻게 진화해도 발기 함수는 변하지 않습니다. 이것이 "1개의 구현, 4개의
결과 형식"의 장치입니다.

## 3열 대조: 결여 ↔ 구현 ↔ 장소

| 능력 | sasio 현황 | asio 구현 | asio 장소 |
| --- | --- | --- | --- |
| 콜백(기본 형식) | 있음(유일한 형식) | `async_result` 기본 특수화 | `asio/async_result.hpp` |
| future | 없음 | `use_future` token | `asio/use_future.hpp` |
| coroutine 접근 | 있음(`co_spawn.hpp` 명시 어댑터, token 없음) | `use_awaitable` token + `co_spawn` | `asio/use_awaitable.hpp`, `asio/co_spawn.hpp` |
| 지연 시작 | 없음 | `deferred`(token이 그 자체로 callable) | `asio/deferred.hpp` |
| 결과 폐기 | 없음(handler 필수) | `detached`(no-op handler) | `asio/detached.hpp` |
| 발기 함수 골격 | 직접 작성 | `async_initiate`가 token과 발기를 분리 | `asio/async_result.hpp`의 `initiate` |
| 완료 시그니처 검사 | 없음 | `completion_signature` 컨셉 | `asio/async_result.hpp`(`is_completion_signature`) |

규모 비교: sasio의 `co_spawn.hpp`(113줄)는 1개 형식 — "coroutine 접근"을
담당합니다. asio의 `use_awaitable.hpp` + `impl/co_spawn.hpp` + `async_result`
체계는 **임의의** 형식에 같은 발기 구현을 제공합니다. 추가된 것은 "더 나은
coroutine"이 아니라, "결과 형식 자체가 확장점이 되었다"는 것입니다.

## 연습

**실험**: sasio에 수동으로 최소 future token을 작성합니다. 제네릭은 요구하지
않고, 의미론만: `read_with_future(socket, buffer)`가
`std::future<std::size_t>`를 반환합니다(오류는 예외로):

```cpp
std::future<std::size_t> read_with_future(
    sasio::ip::tcp::socket& socket, sasio::mutable_buffer buf)
{
  auto promise = std::make_shared<std::promise<std::size_t>>();
  auto future = promise->get_future();
  socket.async_read_some(buf,
      [promise](sasio::error_code ec, std::size_t n) mutable
      {
        if (ec)
          promise->set_exception(
              std::make_exception_ptr(std::system_error(ec)));
        else
          promise->set_value(n);
      });
  return future;
}
```

실행한 후, 제8장과 비교하세요. 이 10줄 함수가 하는 것은 `write_op`의
shared_ptr 릴레이와 같은 종류입니다 — "완료 시에 하는 동작"을 상태와 함께
엔진에 넘깁니다. 한 걸음 더: asio는 결과 형식마다 `read_with_xxx`를 쓰지
않는 이유는? 답은 `async_initiate` 분할 안에 있습니다.

**버그 사냥**: 제11장의 sasio coroutine 어댑터(`co_spawn.hpp`의 자유 함수)는
입구마다 1개입니다. asio에서는 같은 연산이 콜백과 `use_awaitable`을 어떻게
동시에 지원하는가? `asio/use_awaitable.hpp`를 열고, `async_result` 특수화의
"token이 완료 handler가 되는" 부분을 찾아 답하세요: 완료 handler가 호출될 때,
coroutine을 재개하는 것은 누구입니까? (따라가면 `awaitable_frame::resume`에
도달합니다 — 제11장 `awaiting_.resume()`의 asio에서의 동명 형제입니다.)

---

executor와 completion token의 2계층이 모두 분해되었습니다. 이 책의 시작
질문에 이제 답할 수 있습니다: asio의 169,063줄 중, 무엇이 모델이고 무엇이
기계인가? 다음 장은 완전한 읽기 지도를 제시합니다 — sasio의 9장을 asio의
소스 트리에 대응시키고, 6주 읽기 계획을 첨부하여.
