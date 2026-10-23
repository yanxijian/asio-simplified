# Executor 与 completion token：asio 的「组合层」

> **English**：[../en/executor-and-tokens.md](../en/executor-and-tokens.md)
>
> **定位**：概念说明，解释 sasio 为什么没有这两层，以及缺了它们意味着什么。不是 API 参考。

## 它们各自回答一个问题

asio 的异步引擎（`io_context`、scheduler、reactor、handler 队列）只负责「操作完成后调用 handler」。
在这之上还有两个正交的问题：

| 问题 | 机制 | 典型 API |
| --- | --- | --- |
| 这个 handler 在**哪个执行体**上跑？ | executor 框架 | `associated_executor`、`bind_executor`、`strand` |
| 调用者想**怎么拿到结果**？ | completion token | `async_result`、`use_future`、`use_awaitable`、`deferred` |

引擎决定「什么时候完成」，这两层决定「完成后在哪里、以什么形式交回调用者」。

## executor：完成投递策略

在 asio 里，每个 handler 都关联一个 executor（默认是 I/O 对象的 executor）。`post`/`dispatch`/`defer`
通过这个关联决定投递到哪。`strand` 本质上是一个 **executor 适配器**：

```cpp
asio::async_read(socket, buffer,
    asio::bind_executor(strand, handler));   // 整条组合链的完成都串行化
```

关键点是「一次绑定，处处生效」：组合操作内部每一层中间 handler 都会继承这个关联，而不是只包住最后
一次回调。再往上还有 `execution::` 属性（`require(blocking.never)`、`prefer(relationship.continuation)`、
`execution::allocator`），用来表达「不许阻塞」「这是续作」「用哪个分配器」。

## completion token：结果获取方式

asio 每个发起函数的最后一个参数都是 token。`async_result<Token, Signature>` 把「token + 完成签名」
映射成返回类型与启动方式，于是同一个 `socket.async_read_some(buffer, token)` 可以写成：

- 普通回调：接收 `(error_code, size_t)` 的函数
- future：`asio::use_future`
- 协程：`asio::use_awaitable`
- 延迟启动：`asio::deferred`
- 丢弃结果：`asio::detached`，或 `as_tuple` / `redirect_error` 改变签名

它的价值是**不用为每种结果形式复制一套 API**，并且第三方可以定义自己的 token。

## any_io_executor：类型擦除

`any_io_executor` 是满足 executor 要求的类型擦除容器（1.74 起成为 I/O 对象的默认 executor 参数）。
它让 `basic_stream_socket<tcp, any_io_executor>` 能接受 `io_context`、`thread_pool`、`strand` 等任意
执行体，而不必为每种组合实例化一个新 socket 类型。代价是每次投递多一层间接调用和可能的堆分配。

## sasio 的选择

| 能力 | asio | sasio |
| --- | --- | --- |
| 完成投递到指定 strand/线程池 | `bind_executor`，组合操作自动继承 | 只有 `strand.post`，手动包装 |
| 同一 API 支持回调 / future / 协程 | completion token | 回调 + 专用 `awaitable` 适配器 |
| 运行时更换执行体 | `any_io_executor` | 不需要：只有一个 `io_context` |
| 执行策略属性 | `execution::` | 无 |

原因不是「这些功能没用」，而是它们属于**通用化设施**：把「在哪儿跑、怎么拿结果」做成参数，代价是
asio 最难读的那部分模板与类型擦除。对一个用来读代码的 4,100 行版本，保留引擎、砍掉组合层是划算的。

## 手工近似 `bind_executor`

[`examples/ex04_bind_executor.cpp`](../../examples/ex04_bind_executor.cpp) 用一个 10 行的包装演示
asio 的 `bind_executor` 在手工版里长什么样：

```cpp
std::function<void(error_code)> bind_strand(strand& s,
    std::function<void(error_code)> handler)
{
  return [&s, handler = std::move(handler)](error_code ec) {
    s.post([handler, ec]() mutable { handler(ec); });
  };
}
```

例子让 24 个定时器在两个 `run()` 线程上完成，所有完成 handler 都经 `bind_strand` 投进同一个 strand，
最后打印 `max_active=1`。它演示了「完成投递到指定执行体」，但**做不到** asio 的自动继承：组合操作的
内部层不会自动经过这个包装，只有你包住的那个 handler 会。

## 什么时候才值得补

1. **executor + `bind_executor`**：如果要把 asio 例子/第三方代码直接移植过来，这是第一个缺口；
   成本约 250–350 行，且需要改动每个完成点与组合操作的传递路径。
2. **completion token**：如果只需要 future 或统一协程入口，可以加少量重载；要做得不误导，就得接近
   `async_result`，成本 300–500 行。
3. **`any_io_executor`**：只有引入 `thread_pool` 或多个 context、需要运行时选择时才值得；单一
   `io_context` 下它是纯开销。

在此之前，README 里的约束仍然有效：多线程 `run()` 时完成 handler 落在完成它的线程；要串行请自己包
`strand`。
