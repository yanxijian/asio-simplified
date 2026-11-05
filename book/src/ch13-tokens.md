# completion token 层：结果获取方式

executor 层回答「在哪儿跑」，第 13 章——也就是本章——回答另一半：**结果以什么形式交回调用者**。同一个发起函数，

```cpp
socket.async_read_some(buffer, /* what goes here? */);

```

不同调用方想要的东西完全不同：有人要回调，有人要 `std::future`，有人要协程，有人根本不在乎结果（发完就忘）。asio 的答案是把「这里放什么」做成编译期参数——**completion token**，用一套特化机制让同一个发起函数对每种结果形式只写一份实现。

## 设计：token 是编译期的策略

先把机制压成三条规则：

1. **每个发起函数声明自己的完成签名**。`async_read_some` 的签名是 `void(error_code, std::size_t)`；发起函数自己不处理 token，它只把签名和 token 一起交给 `async_result`；
2. **`async_result<Token, Signature>` 按特化决定两件事**：发起函数的返回类型，以及「真正传给引擎的完成 handler 是什么形态」。默认特化（token 就是普通回调）返回 `void`、handler 就是 token 本身——你手写的那 20 年回调代码，走的都是这个默认特化；
3. **用户可以自定义 token**。给一个新类型特化 `async_result`，它就成为这个库的一等公民——`use_future`、`use_awaitable`、`deferred`、`detached` 全是这个机制的用户，不是引擎的硬编码。

sasio 没有这套机制：回调是唯一形式，协程靠 `co_spawn.hpp` 里的显式适配器。本章拆开 asio 的特化体系，并回答「如果给 sasio 装，最小要装什么」。

## trace：四种 token 的四种路径

同一个发起调用，四种 token，四条路径：

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

第 2 章讲 trace 时说过「发起函数返回什么、handler 是谁」——现在可以精确了：**这两件事都由 `async_result` 特化决定**。发起函数的形状被规约成固定的骨架（`async_initiate`，见下），可变的只有 token。

## 精读：async_result 的主模板

`asio/async_result.hpp` 的主模板（去掉文档注释后就是这几个成员）：

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

主模板描述的是**默认行为**（token 即回调，返回 void）；每个 token 类型提供自己的特化，改掉 `return_type` 与 `completion_handler_type`。`initiate` 是关键设计——它把发起函数劈成两半：

- **token 处理**（特化负责）：use_future 的特化会构造 promise、把「给 promise 赋值」的对象作为 completion handler、把 `future` 作为返回值；
- **实际发起**（`Initiation` 负责）：真正去调 socket 的 `async_read_some` 实现，拿到处理好的 completion handler，登记进引擎。

劈开之后，发起函数的作者只写一次 `Initiation`；token 体系怎么变，发起函数不动。这就是「一份实现、四种结果形式」的机关。

## 三栏对照：缺位 ↔ 实现 ↔ 去处

| 能力 | sasio 现状 | asio 实现 | 去 asio 哪里找 |
| --- | --- | --- | --- |
| 回调（默认形式） | 有（唯一形式） | `async_result` 默认特化 | `asio/async_result.hpp` |
| future | 无 | `use_future` token | `asio/use_future.hpp` |
| 协程接入 | 有（`co_spawn.hpp` 显式适配器，无 token） | `use_awaitable` token + `co_spawn` | `asio/use_awaitable.hpp`、`asio/co_spawn.hpp` |
| 延迟启动 | 无 | `deferred`（token 即一个可调用对象） | `asio/deferred.hpp` |
| 丢弃结果 | 无（handler 必须存在） | `detached`（空操作 handler） | `asio/detached.hpp` |
| 发起函数骨架 | 发起函数直接写 | `async_initiate` 劈分 token 与发起 | `asio/async_result.hpp` 的 `initiate` |
| 完成签名检查 | 无 | `completion_signature` 概念 | `asio/async_result.hpp`（`is_completion_signature`） |

规模感的对照：sasio 的 `co_spawn.hpp`（113 行）承担了「协程接入」一种形式；asio 的 `use_awaitable.hpp` + `impl/co_spawn.hpp` + `async_result` 体系为**任意**形式提供同一份发起实现。多出来的不是「更好的协程」，是「结果形式本身成了可扩展点」。

## 练习

**实验**：给 sasio 手工写一个最小 future token。不求泛型，只求语义：一个 `read_with_future(socket, buffer)` 函数，返回 `std::future<std::size_t>`（错误用异常传）：

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

跑通后对照第 8 章：这个 10 行函数做的事，与 `write_op` 的 shared_ptr 接力是同一类——把「完成时的动作」连同状态一起交给引擎。再想一步：asio 为什么不给每个结果形式写一个 `read_with_xxx`？答案在 `async_initiate` 的劈分里。

**bug 狩猎**：第 11 章的 sasio 协程适配器（`co_spawn.hpp` 的 `async_wait` 等）每个入口一个自由函数。对照 asio：同一个操作怎么同时支持回调与 `use_awaitable`？打开 `asio/use_awaitable.hpp`，找 `async_result` 特化里「把 token 变成 completion handler」的那段，回答：completion handler 被调用时，是谁恢复了协程？（顺着它你会看到 `awaitable_frame::resume`——第 11 章的 `awaiting_.resume()` 在 asio 里的同名兄弟。）

---

executor 与 completion token 两层都拆完了。现在可以回答全书开头的问题了：16 万行的 asio，哪些是模型、哪些是机械？下一章给出完整的阅读地图——包括把 sasio 的九章映射到 asio 的源码树，和一份 6 周阅读计划。
