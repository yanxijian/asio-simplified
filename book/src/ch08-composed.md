# 组合操作：operation 套 operation

第 7 章的 echo 服务器里有一段不那么体面的代码——`session::do_write_some`：

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

这就是「把一个缓冲区写完」的真实代价：手动维护偏移量、手动续发、手动处理错误——而且这段逻辑**每个**需要完整写的调用点都要抄一遍。写是这样，读更糟（TCP 是字节流，一次 `recv` 给多少全看内核心情，「读满 N 字节」注定要循环）。

本章把这类循环抽成一个可复用的操作：`async_write(stream, buffer, handler)` 和 `async_read(stream, buffer, handler)`——发起一次，完成时保证「整个缓冲区已处理完」（或带着明确的错误）。它们不做任何新的系统调用，只是**把多次 `async_write_some` / `async_read_some` 串成一条链**，所以叫组合操作。

## 设计：一个被接力棒活捉的状态机

组合操作的状态少得惊人：

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

两条不变量：

1. **状态对象的生命期 = 整条操作链**。每个中间完成 handler 都捕获 `shared_from_this()`，链走到哪，状态活到哪；链结束（最终完成被调用），最后一个捕获释放，状态自动销毁。没有泄漏，没有悬垂，没有手动 delete；
2. **每次完成只做一次推进，然后发起下一段**。`transferred_ += bytes` 之后，下一次 `async_write_some` 只传剩余区间（`buffer_.data() + transferred_`，长度 `buffer_.size() - transferred_`）。推进的终点有四个：写满（成功）、出错、零字节返回（防御性终止，防止自旋）。

这个形状——**每次完成创建下一段发起**——就是「组合」的全部机理。它不碰 reactor，不碰队列，纯粹用回调链把简单操作粘成复杂操作。asio 的 `async_read` / `async_write` 同理，只是粘的方式换成了可插拔的完成条件（见对照表）。

## 一次 100 字节写入的一生

假设发送缓冲区只能一次吞下 60 字节：

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

中间出现了两个**匿名中间 handler**（w1、w2）——它们是 lambda，捕获着 `self`（write_op）。用户只发起了一次 `async_write`，引擎里却跑了两次真实的 `send`、post 了两个中间完成；**最终 handler 是在 w2 的调用栈里由 `complete()` 直接调用的**——没有第三次入队，但仍在 run 循环内执行，契约二不被破坏。契约一同样在整条链上成立：每个中间 handler 都是经 `post` 投递的，没有任何一环在发起函数里就地执行。

链的终止也值得注意：w2 执行时 `transferred_ == buffer_.size()`，`do_write` 开头就判定完成——**最后一圈不发起新操作**，直接调最终 handler。中间 handler 逐个释放 `self`，w2 执行完毕、释放最后一个引用，write_op 连同它的状态一起销毁。不变量 1 的「活到链尾」就是这么自动实现的。

## 精读：write.hpp 的接力核心

看 `include/sasio/write.hpp` 的正文（省略构造与成员声明）：

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

四个细节，一个比一个容易被忽略：

- **`auto self = shared_from_this()` 在发起前捕获**。lambda 持有一个引用，从「发起下一段」到「下一段的完成回调执行」整个窗口里，状态都活着。少这一行，中间完成执行时 `write_op` 可能已经销毁——这是回调链最经典的 use-after-free；
- **`complete` 先 move 走 handler 再置空**。最终 handler 只会被调用一次——即使未来有人改出双路径同时完成的 bug，第二次也会在 `if (handler)` 处停下。一次性语义靠「取走」而非「记得」来保证；
- **零字节写是终止条件而不是循环条件**。`async_write_some` 返回 0 在语义上没有推进，接着续发只会永远返回 0——把它当作完成（带已写字节数），与 asio 的处理一致；
- **出错时报告的是 `transferred_`，不是 0**。「写了 60 字节后对端断了」的完成是 `(connection_reset, 60)`——调用方需要知道已经发生的那部分。`async_read` 对 EOF 也一样，只是错误码换成 `eof`。

`async_write` 本身只有一层薄壳，其中一个分支值得单独看——**空缓冲**：

```cpp
if (buffer.size() == 0)
{
  stream.context().post([erased]() mutable {
    erased(error_code(), 0);
  });
  return;
}

```

什么都没做，也走 `post`——不是直接调 `handler(error_code(), 0)`。契约二的又一次体现：哪怕整条链退化成零工作，完成也必须发生在 `run()` 里。这个分支保证了 `async_write(s, buffer(s, 0), h)` 与 `async_write(s, 大缓冲, h)` 的完成语义完全同构。

`read.hpp` 与 write 完全对称，只有两处不同：终点从「写满」变成「读满」；**对端关闭（`async_read_some` 返回 0 字节）被视为 `eof` 错误**——缓冲区永远读不满了，带着已读字节数、以 `sasio::eof` 完成。注意与 `async_read_some` 的区别：后者返回 0 字节是「对端关闭」的正常报告，前者把它升格成错误，因为「读满」这个承诺已经不可能实现。

## 对照表

| 问题 | asio | sasio |
| --- | --- | --- |
| 状态机 | `impl/write.hpp` 与 `impl/read.hpp` 里的 `detail::write_op` / `read_op` | `include/sasio/write.hpp` / `read.hpp` 的同名类 |
| 完成条件 | 可插拔：`transfer_all()`（默认）、`transfer_exactly(n)`、`transfer_at_least(n)`，挂在 `detail/base_from_completion_cond.hpp` | 硬编码「全部」 |
| 缓冲序列 | `ConstBufferSequence`（多缓冲 + scatter/gather） | 单个 `const_buffer` / `mutable_buffer` |
| EOF 语义 | `async_read` 以 `error::eof` 完成 | 同（`sasio::eof`） |
| 流式变体 | `async_read_until`（动态缓冲 + 分隔符） | 无（差异表登记） |

asio 的完成条件值得一读：`base_from_completion_cond` 把「还差多少才算完」做成策略基类，`write_op` 每次完成时问它一句。sasio 硬编码了 `transfer_all` 的语义——这是「砍掉通用化、保留主干」的又一次体现，而练习会把它加回来。

## 练习

**实验**：把 echo 服务器升级到第 2 形态——`session::do_write_some` 手写循环整体删除，`do_read` 的完成里直接调：

```cpp
sasio::async_write(socket_, sasio::buffer(data_, length),
    [this, self](error_code ec, std::size_t)
    {
      if (ec)
        return;
      do_read();
    });

```

功能与手写循环完全一致，但 `write_offset_`、`write_size_` 两个成员和整个 `do_write_some` 都消失了——状态移入了 `write_op`，生存期由 shared_ptr 接力管理。对照两版代码回答：手写版的 `write_offset_` 在哪？组合版的对应状态在哪？为什么后者不需要 `session` 持有它？

**bug 狩猎 + 扩展**：给 sasio 加 `transfer_at_least(n)`——读到或写到**至少** n 字节就算完成（asio 的同名完成条件）。提示：给 `read_op` / `write_op` 加一个 `std::size_t least_` 成员和构造参数，两处完成判定从 `transferred_ == buffer_.size()` 改成 `transferred_ >= least_`（或合写）；`async_read` / `async_write` 加一个带 `least` 参数的重载。做完后想一个问题：`transfer_at_least(0)` 恰好等于现在的默认行为吗？`async_read_some` 返回 0 字节加 `eof` 的组合，在 `least_ = 0` 时应该报错还是成功？答案要与 asio 对齐，去 `base_from_completion_cond.hpp` 里找它的判定顺序。

---

一次读一个缓冲区、一次写一个缓冲区，配上 UDP 和域名解析，一个能用的网络库的 API 面就齐了。下一章把最后两块拼上：UDP 的 `send_to` / `receive_from`（同一 reactor 的第二种用法），和把「域名变地址」丢给工作线程的 resolver（唯一一个用线程的组件）。
