# socket：发起函数只描述「就绪后做什么」

引擎三章拆完，工具都齐了：队列管排队，reactor 管就绪，定时器管时间。从本章开始把这套引擎用起来——先看一段 `src/ip_tcp.cpp` 的真实代码，`async_read_some` 的发起函数：

```cpp
void socket::start_read_some(mutable_buffer buffer,
    std::function<void(error_code, std::size_t)> handler)
{
  if (!is_open())
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(bad_descriptor, 0);
    });
    return;
  }

  if (read_pending_)
  {
    scheduler_.post([handler = std::move(handler)]() mutable {
      handler(already_started, 0);
    });
    return;
  }

  read_pending_ = true;
  read_buffer_ = buffer;
  read_handler_ = std::move(handler);
  reactor_.start_read(handle_, [this]() { do_read(); });
}

```

找了三遍，你没有找到 `recv`。这个函数做了且只做了四件事：校验状态、把缓冲区和 handler **存进成员**、登记一个「可读时叫我」的兴趣、返回。真正的 `recv` 在另一个函数 `do_read` 里，它只会在 reactor 报告就绪时被调用。

本章把「发起」与「完成」两段都拆开——这是 asio 所有 socket 代码的共同形状，`basic_stream_socket` 的每一个 `async_*` 都是如下形状。

## 设计：一个方向一个槽位

socket 的状态不多，但每个成员都有讲究：

```cpp
SOCKET handle_ = INVALID_SOCKET;

bool read_pending_ = false;
std::function<void(error_code, std::size_t)> read_handler_;
mutable_buffer read_buffer_;
// a symmetric set for the write direction

```

三条不变量：

1. **每个方向至多一个挂起操作**。`read_pending_` 就是占位符——第二次发起读，会得到一个经 `post` 投递的 `already_started`（注意仍是 post，契约一在错误路径上也不破例）。asio 用 operation 队列支持任意多个，sasio 用一格槽位，差异在容量不在形状；
2. **read/write/accept 的发起函数不做数据面系统调用**；connect 例外——发起时要做一次非阻塞 connect 尝试（它没有「就绪」可言：要么立刻成功，要么立刻失败，要么进行中，第三种才登记写就绪兴趣）；
3. **完成的投递一律经 `post`**，包括立即失败——`bad_descriptor` 不是就地抛出而是排队投递，保证 handler 永远在 `run()` 里执行（契约二）。

还有一处必须点名的边界：**缓冲区和 handler 的生存期是发起方的责任**。`read_buffer_` 存的是指针和长度，不是数据副本——挂起期间调用方必须保证缓冲区活着。asio 相同，这是 C++ 异步 I/O 的通行约定。

## 一次读操作的两段式

把第 2 章的时序图落到代码上。**第一段（发起）**就是上面的 `start_read_some`，结束于 `reactor_.start_read(handle_, [this]() { do_read(); })`。

**第二段（就绪）**由 reactor 在 `WSAPoll` 返回后调起：

```cpp
void socket::do_read()
{
  if (!read_pending_)
    return;

  error_code ec;
  const int result = detail::socket_ops::recv_socket(handle_,
      read_buffer_.data(), clamp_to_int(read_buffer_.size()), ec);

  if (result < 0)
  {
    if (ec == would_block)
    {
      // Spurious readiness: register again and wait.
      reactor_.start_read(handle_, [this]() { do_read(); });
      return;
    }

    complete_read(ec, 0);
    return;
  }

  complete_read(error_code(), static_cast<std::size_t>(result));
}

```

这里才是 `recv` 的实现位置。三种出口：

- **假警报**：poll 说可读，`recv` 却返回 `would_block`（数据在到达检查和调用之间被并发连接拿走了，或 poll 的水平触发语义本身就带一点模糊）。处理方式是重新登记、回去继续等——第 5 章不变量 2 的「移出再执行」决定了必须显式重新登记，否则这个读操作就从 reactor 的世界里消失了；
- **真错误**：`complete_read(ec, 0)`——清占位符、注销兴趣、把 handler 连同错误码 `post` 出去；
- **成功**：`complete_read(空错误码, n)`，同上。

`complete_read` 本身十几行：清 `read_pending_`、注销 reactor 兴趣、把 handler 移出成员、`post`。成员槽位清空意味着**槽位可以立刻被下一次发起复用**——上一个 handler 可能还在队列里没执行，但新的 `async_read_some` 已经合法了。handler 与槽位的生命期在此解耦。

## 两个不对称：connect 与 close

**connect 没有同步的「结束」可言，是三态的**。发起函数 `start_connect` 直接调用非阻塞 `connect`：

- 立即成功（本机回环常见）：`complete_connect(空错误码)`；
- 立即失败（目标不可达等）：`complete_connect(ec)`；
- `would_block`（连接进行中）：登记写就绪兴趣，等内核把三次握手做完。

第三种就绪后，`do_connect` 不写任何数据，只做一件事——`getsockopt(SO_ERROR)` 读出连接的最终结果，然后 `complete_connect`。**「可写」在这里的含义是「握手完成或失败」，结果要去 SO_ERROR 里领**。这是非阻塞 connect 的标准姿势，asio 的 `detail/reactive_socket_connect_op` 同款。

**close 会注入完成，而不是撤销它们**。`socket::close()` 先 `cancel()`：对每个挂起方向调 `complete_xxx(operation_aborted)`——handler 被 `post` 出去，带着「操作被取消」的错误码；然后才注销描述符、关句柄。所以 close 之后你**必然**还会收到每个挂起操作的完成通知，只是错误码是 `operation_aborted`。第 6 章定时器的「取消不等于没有通知」在 socket 上原样重演，理由也一样：调用 `close()` 的栈可能就是 handler 想碰的栈，就地调用等于重入。

acceptor 是 socket 的特例：`async_accept` 登记读就绪（新连接到达 = 监听 socket 可读），`do_accept` 里 `accept()` 拿到新句柄、设非阻塞、`peer->assign()` 交给调用方提供的 socket 对象。形状与读完全同构，不再展开。

## 精读：cancel 与 complete_read

把「取消」的两段代码连起来读，这是 socket 生命周期的收尾动作：

```cpp
void socket::cancel()
{
  if (!is_open())
    return;

  if (connect_pending_)
    complete_connect(operation_aborted);
  if (read_pending_)
    complete_read(operation_aborted, 0);
  if (write_pending_)
    complete_write(operation_aborted, 0);
}

void socket::complete_read(error_code ec, std::size_t bytes)
{
  read_pending_ = false;
  reactor_.stop_read(handle_);

  std::function<void(error_code, std::size_t)> handler =
      std::move(read_handler_);
  if (!handler)
    return;

  scheduler_.post([handler = std::move(handler), ec, bytes]() mutable {
    handler(ec, bytes);
  });
}

```

三个细节：

- `complete_xxx` 是幂等的收尾口：清标志、注销、**把 handler 移出成员**、post。此后 socket 成员里不再有这个操作的痕迹，析构、close、下一次发起都安全；
- post 的 lambda **按值捕获了 handler 和结果**——它不再引用 socket 的任何成员。这就是 close 能在 complete 之后立刻关句柄的原因：投递出去的通知是自足的；
- `if (!handler) return;` 兜的是「槽位空着却被 cancel」的退化情形——防御性编程，正常路径到不了。

对照 asio：`basic_socket::cancel` 走 `reactor.cancel_ops(descriptor)`，reactor 把该描述符的整个 operation 队列逐个置 `operation_aborted` 投递。机制不同（队列 vs 槽位），行为相同（完成必达、绝不重入）。

## 对照表

| 问题 | asio | sasio |
| --- | --- | --- |
| 发起函数 | `basic_stream_socket.hpp` 的 `async_read_some` | `src/ip_tcp.cpp` 的 `start_read_some` |
| 完成载体 | `detail/reactive_socket_recv_op.hpp`（operation 对象，含 handler/allocator/工作单位） | 成员槽位 + 捕获 handler 的 `post` lambda |
| 挂起数量 | 每方向一队列 | 每方向一格 |
| connect | `reactive_socket_connect_op` + SO_ERROR | `start_connect` / `do_connect` 同款流程 |
| 取消 | `reactor.cancel_ops` 逐 op 投递 | `cancel()` 逐槽位投递 |
| 句柄层 | `detail/socket_ops.hpp` + `impl/socket_ops.ipp`（5,140 行） | `src/socket_ops.cpp` + 头文件（259 行） |

asio 的 operation 对象值得多说一句：它是「能活到事件发生的状态载体」，把 handler、分配器、work 单位、完成结果打包成一个堆上对象。sasio 用成员槽位代替了它——代价是每方向一个挂起（不变量 1），好处是本章可以专注在两段式本身。第 8 章的组合操作会再次需要「活到链尾的状态」，那时你会看到 shared_ptr 接力。

## 练习

**实验**：给 ch2 的 echo 服务器换上 socket 级操作。基于 `examples/ex03_echo_server.cpp` 的 `session::do_read` / `do_write_some`，回答并验证：两个连接同时各发 1KB，单线程的 `run()` 里这两个 session 的 `do_read` 会交错执行吗？在两个 handler 里各打印一个标记和时间点验证你的答案。（预期：会交错——每个 session 的每次 `async_read_some` 完成是独立的队列项，谁就绪处理谁。）

**bug 狩猎**：`socket::close()` 之后、投递的 `operation_aborted` handler 执行之前，用户代码立刻对同一个 socket 对象发起了新的 `async_read_some`。此时 `handle_` 已经是 `INVALID_SOCKET`，`start_read_some` 会走哪个分支？handler 拿到什么错误码？两次通知（旧的 aborted、新的 bad_descriptor）的先后顺序有保证吗？先从代码推出答案，再写 20 行程序验证。

---

一次 `async_read_some` 拿回来的往往不是全部数据——TCP 是字节流，一次 `recv` 给多少全看内核心情。把「反复 read_some 直到读满」这件事做成一个通用的、能自己接力的操作，就是下一章的组合操作 `async_read`。
