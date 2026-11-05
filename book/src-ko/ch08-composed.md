<!-- translated-from: book/src/ch08-composed.md sha256:6033fce1cb13 -->

# 합성 연산: operation 위에 operation

제7장의 echo 서버에는 그다지 보기 좋지 않은 코드가 있었습니다 —
`session::do_write_some`:

```cpp
void do_write(std::size_t length)
{
  write_size_ = length;
  write_offset_ = 0;
  do_write_some();
}

void do_write_some()
{
  auto self = shared_from_this();
  socket_.async_write_some(
      sasio::buffer(data_ + write_offset_, write_size_ - write_offset_),
      [this, self](error_code ec, std::size_t sent) {
        if (ec)
          return;
        write_offset_ += sent;
        if (write_offset_ < write_size_)
          do_write_some();       // not finished, continue
        else
          do_read();
      });
}
```

이것이 "버퍼를 모두 쓰기"의 실제 대가입니다: 수동 오프셋, 수동 재발기, 수동
오류 처리 — 그리고 이 로직은 완전한 쓰기가 필요한 **모든** 호출 지점에서
복사해야 합니다. 쓰기는 이 정도이고, 읽기는 더 나쁩니다(TCP는 바이트
스트림이며, 한 번의 `recv`가 얼마를 줄지는 커널 마음대로 — "N바이트를 정확히
읽기"는 반드시 루프가 됩니다). 이 장은 이런 루프를 재사용 가능한 연산으로
추상화합니다: `async_write(stream, buffer, handler)`와
`async_read(stream, buffer, handler)` — 발기는 한 번이고, 완료 시에는 "버퍼
전체가 처리됨"(또는 명확한 오류)이 보장됩니다. 새로운 시스템 콜은 하지
않습니다. 여러 번의 `async_write_some` / `async_read_some`을 하나의 체인으로
연결할 뿐입니다. 그래서 합성 연산(composed operation)이라고 부릅니다.

## 설계: 릴레이 배턴에 붙잡힌 상태 기계

합성 연산의 상태는 놀랄 만큼 적습니다:

```cpp
template <typename Stream>
class write_op : public std::enable_shared_from_this<write_op<Stream>>
{
  Stream& stream_;            // target (a reference, not owned)
  const_buffer buffer_;       // the whole buffer to write
  std::function<void(error_code, std::size_t)> handler_;  // final completion
  std::size_t transferred_ = 0;   // bytes written so far
};
```

불변식 2개:

1. **상태 객체의 수명 = 연산 체인의 수명**. 모든 중간 완료 handler가
   `shared_from_this()`를 캡처합니다; 체인이 어디까지 진행되든, 상태는 거기까지
   살아 있습니다. 체인이 끝나면(최종 완료가 실행되면), 마지막 캡처가 해제되고
   상태는 자동으로 파괴됩니다. 누수 없음, 댕글링 없음, 수동 delete도 없음;
2. **각 완료는 한 번만 전진하고, 그다음 세그먼트를 발기합니다**.
   `transferred_ += bytes` 후에, 다음 `async_write_some`은 남은 범위만
   전송합니다(`buffer_.data() + transferred_`, 길이
   `buffer_.size() - transferred_`). 전진의 종점은 네 개입니다: 만원(성공),
   오류, 0바이트 반환(스핀 방지의 방어적 정지).

이 형태 — **각 완료가 다음 발기를 만듭니다** — 가 "합성"의 전체 기제입니다.
reactor도 큐도 건드리지 않습니다; 콜백 체인으로 단순 연산을 복잡한 연산에
붙일 뿐입니다. asio의 `async_read` / `async_write`도 같은 방식이며, 접착 방식이
교체 가능한 완료 조건으로 바뀐 것뿐입니다(대조표 참조).

## 100바이트 쓰기의 생애

송신 버퍼가 한 번에 60바이트만 받아들인다고 가정합니다:

```text
async_write(s, 100B, done)
 └─ write_op constructed (transferred_=0)
     └─ s.async_write_some(buf[0..100))
         └─ poll reports writable → do_write → send writes 60
             └─ post(w1: transferred_=60, 60<100 → continue)
                 └─ s.async_write_some(buf[60..100))
                     └─ poll reports writable → do_write → send writes 40
                         └─ post(w2: transferred_=100 → complete(empty error))
                             └─ done(empty error, 100)
```

두 개의 **익명 중간 handler**(w1, w2)가 나타납니다 — `self`(write_op)를
캡처한 lambda입니다. 사용자는 한 번 `async_write`를 발기했지만, 엔진 안에서는
진짜 `send`가 2번 실행되고 중간 완료가 2번 post되었습니다. **최종 handler는
w2의 호출 스택 안에서 `complete()`가 직접 호출합니다** — 세 번째 입대는 없지만,
여전히 run 루프 내의 실행이므로 계약 2는 깨지지 않습니다. 계약 1도 체인
전체에서 실현됩니다: 모든 중간 handler는 `post`로 전달되며, 발기 함수 안에서
인라인 실행되는 단계는 어디에도 없습니다.

체인의 종단도 주목할 만합니다: w2가 실행될 때 `transferred_ == buffer_.size()`
이므로, `do_write`는 시작에서 완료를 판정합니다 — **마지막 주기는 새로운
연산을 발기하지 않고**, 직접 최종 handler를 호출합니다. 중간 handler가 `self`를
순서대로 해제하고, w2가 끝나고 마지막 참조를 해제하면, write_op는 상태와
함께 파괴됩니다. 불변식 1의 "체인 끝까지 살아 있는"이 자동으로 실현됩니다.

## 정독: write.hpp의 릴레이 핵심

`include/sasio/write.hpp`의 본문에서(구축과 멤버 선언 생략):

```cpp
void start() { do_write(); }

private:
void do_write()
{
  if (transferred_ == buffer_.size())       // full: done successfully
  {
    complete(error_code());
    return;
  }

  auto self = this->shared_from_this();     // ← the relay baton
  const char* data = static_cast<const char*>(buffer_.data()) + transferred_;

  stream_.async_write_some(
      const_buffer(data, buffer_.size() - transferred_),   // only the rest
      [self](error_code ec, std::size_t bytes) {
        self->transferred_ += bytes;

        if (ec)
        {
          self->complete(ec);               // error: finish with bytes written
          return;
        }

        // A zero-byte write would spin; treat it as completion.
        if (bytes == 0)
        {
          self->complete(error_code());     // zero bytes: defensive stop
          return;
        }

        self->do_write();                   // advance and re-initiate
      });
}

void complete(error_code ec)
{
  std::function<void(error_code, std::size_t)> handler = std::move(handler_);
  handler_ = nullptr;
  if (handler)
    handler(ec, transferred_);
}
```

4개의 세부사항, 하나씩 쉽게 간과되는 것들:

- **`auto self = shared_from_this()`는 발기 전에 캡처합니다**. lambda는
  "다음 세그먼트를 발기한다"부터 "그 세그먼트의 완료가 실행된다"까지의 전체
  창에서 참조를 하나 가지고 있습니다. 상태는 이 기간 내내 살아 있습니다. 이
  줄이 없다면, 중간 완료가 실행될 때 write_op는 이미 파괴되어 있을 수 있습니다
  — 콜백 체인의 가장 고전적인 use-after-free입니다;
- **`complete`는 handler를 move로 끌어낸 후 null로 만듭니다**. 최종 handler는
  정확히 한 번만 호출됩니다 — 설령 미래의 편집이 두 완료 경로를 만들어도, 두
  번째는 `if (handler)`에서 멈춥니다. 1회 한정의 의미론은 "기억하기"가 아니라
  "가져가기"로 보장합니다;
- **0바이트 쓰기는 종료 조건이지 루프 조건이 아닙니다**. `async_write_some`이
  0을 반환하면 전진이 없습니다; 재발기하면 영원히 0을 반환합니다 — 완료로
  처리합니다(쓴 바이트 수를 첨부하여), asio와 같은 처리입니다;
- **오류 시 완료는 `transferred_`를 보고합니다**(0이 아닙니다). "60바이트 쓴
  후에 상대가 끊겼다"의 완료는 `(connection_reset, 60)`입니다 — 호출자는 이미
  일어난 부분을 알 필요가 있습니다. `async_read`도 EOF에서 마찬가지이며, 오류는
  `sasio::eof`로 바뀝니다.

`async_write` 자체는 얇은 껍데기이며, 하나의 분기가 독립적으로 주목할 만합니다
— **빈 버퍼**입니다:

```cpp
if (buffer.size() == 0)
{
  stream.context().post([erased]() mutable {
    erased(error_code(), 0);
  });
  return;
}
```

할 일이 없어도, 여전히 `post`를 통과합니다 — 직접 `handler(error_code(), 0)`을
호출하지 않습니다. 계약 2의 재확인입니다: 체인이 0 작업으로 퇴화해도, 완료는
`run()` 안에서 발생해야 합니다. 이 분기가
`async_write(s, buffer(s, 0), h)`가 다른 모든 `async_write`와 완전히 동형의
완료 의미론을 갖도록 보장합니다.

`read.hpp`는 write와 완전히 대칭이며, 2가지만 다릅니다: 골이 "쓰기 완료"에서
"읽기 완료"로 바뀌고, **상대의 종료(`async_read_some`이 0바이트를 반환)는
`eof` 오류로 승격됩니다** — 버퍼는 영원히 채워질 수 없으므로, "읽기 완료"라는
약속은 더 이상 지켜질 수 없습니다. `async_read_some`과의 대비에 주목하세요:
some형에서 0바이트 반환은 "상대가 닫혔다"의 정상 보고이지만, 합성형에서는
오류로 승격됩니다. "읽기 완료"라는 약속이 더 이상 유지될 수 없기 때문입니다.

## 대조표

| 질문 | asio | sasio |
| --- | --- | --- |
| 상태 기계 | `impl/write.hpp`와 `impl/read.hpp`의 `detail::write_op` / `read_op` | `include/sasio/write.hpp` / `read.hpp`의 동명 클래스 |
| 완료 조건 | 교체 가능: `transfer_all()`(기본), `transfer_exactly(n)`, `transfer_at_least(n)`, `detail/base_from_completion_cond.hpp`에 위치 | 하드코딩된 "모두" |
| 버퍼 시퀀스 | `ConstBufferSequence`(다중 버퍼, scatter/gather) | 단일 `const_buffer` / `mutable_buffer` |
| EOF 의미론 | `async_read`는 `error::eof`로 완료 | 동일(`sasio::eof`) |
| 스트림 변형 | `async_read_until`(동적 버퍼 + 구분자) | 없음(차이표에 등록) |

asio의 완료 조건은 읽을 가치가 있습니다: `base_from_completion_cond`가
"얼마나 남았어야 완료인가"를 정책 기반 클래스로 만들고, `write_op`는 매
완료마다 그것에게 묻습니다. sasio는 `transfer_all`을 하드코딩했습니다 —
일반화를 잘라내고 몸통을 남기는 트레이드오프의 재현이며, 연습이 그것을
다시 추가합니다.

## 연습

**실험**: echo 서버를 제2형태로 업그레이드합니다 — `session::do_write_some`의
수동 루프를 통째로 삭제하고, `do_read`의 완료에서 호출합니다:

```cpp
sasio::async_write(socket_, sasio::buffer(data_, length),
    [this, self](error_code ec, std::size_t)
    {
      if (ec)
        return;
      do_read();
    });
```

행동은 수동 루프와 완전히 일치하지만, `write_offset_`, `write_size_`라는 2개의
멤버와 `do_write_some` 전체가 사라졌습니다 — 상태는 `write_op`로 옮겨지고,
생존 기간은 shared_ptr 릴레이가 관리합니다. 2버전을 나란히 놓고 답하세요:
수동판의 `write_offset_`은 어디에 있었습니까? 합성판의 대응하는 상태는 어디에
있습니까? 왜 후자는 session이 가질 필요가 없습니까?

**버그 사냥 + 확장**: sasio에 `transfer_at_least(n)`을 추가합니다 —
**최소한** n바이트 읽거나 쓰면 완료로 합니다(asio의 동명 완료 조건). 힌트:
`read_op` / `write_op`에 `std::size_t least_` 멤버와 생성자 인수를 추가하고,
2곳의 완료 판정을 `transferred_ == buffer_.size()`에서
`transferred_ >= least_`(또는 합성형)로 변경합니다; `async_read` /
`async_write`에 `least`를 받는 오버로드를 추가합니다. 끝낸 후 물어봅니다:
`transfer_at_least(0)`은 현재의 기본 행동과 정확히 같습니까? 0바이트의
`async_read_some` 반환과 `eof`의 조합에서, `least_ = 0`은 오류입니까
성공입니까? asio와 맞추려면, 판정 순서를 `base_from_completion_cond.hpp`에서
확인하세요 — 실제 순서는 `impl/read.hpp`에 있습니다. 완료 조건은 I/O 전에
검사됩니다.

---

한 번의 읽기, 한 번의 쓰기, UDP와 이름 해결이 갖춰지면, 사용 가능한 네트워크
라이브러리의 API 면이 완성됩니다. 다음 장은 마지막 2피스를 끼웁니다: UDP의
`send_to` / `receive_from`(동일 reactor의 두 번째 사용), 그리고 "도메인 이름을
주소로"를 워커 스레드에 던지는 resolver(라이브러리에서 유일하게 스레드를
가진 컴포넌트).
