# Reactor 与 Proactor：一次异步操作的完整生命周期

先看一段你最熟悉的代码：

```cpp
char buf[1024];
int n = recv(sock, buf, sizeof(buf), 0);

```

`recv` 返回时，结果已经在手里：要么拿到了数据，要么对端关了，要么出错。
函数返回就是句号。这是同步。

同步的问题要凑够两个等待才显形。写一个 TCP 服务器，同一个线程里既要在
`accept` 上等新连接，又要在已连接的 socket 上等数据——`accept` 先阻塞，
已连接的对端就没人管；反过来也一样。两个句号，一条执行路径，必死一个。

最直观的出路是多开线程：一个连接一个线程，每个线程独占自己的 `recv`，
互不挡路。写起来确实省心，也真的能用。代价在连接数上去之后到账——每个线程
一套栈、内核调度要挨个切换、绝大多数线程在 99% 的时间里只是挂着。等连接数
上千上万，线程方案先于你的服务器崩塌。

另一条路是把「等」这件事集中起来：不再让每个调用点自己阻塞，而是**一个线程
盯着一大堆描述符，谁就绪处理谁**。等待从分散在每个 `recv` 里，变成集中在一个
系统调用里。这就是 reactor。

还有第三条路：连「读」「写」这个动作也交出去。发起时只说一句「读好了叫我」，
数据由内核搬进你的缓冲区，框架递到手上的已经是结果。这是 proactor。

三者的差别可以压成一句话：**同步自己做、拿着结果继续；reactor 拿到的是
「可以做了」；proactor 拿到的直接是「做完了」**。本章后面的所有内容，
就是把第三句话在代码里展开。

## 两张面孔

**reactor**：内核只回答「哪些描述符就绪了」，`read`/`write` 还得你自己来。
select、poll、epoll、kqueue 都是这一族，Windows 上的 WSAPoll 也是。它给你的是
**就绪通知**。

**proactor**：I/O 本身由内核完成，你发起时提交缓冲区，回调拿到的直接是
传输了多少字节。Windows 的 IOCP（I/O Completion Port）是教科书实现。它给你的是
**完成通知**。

asio 两张脸都有，按平台切换：

- **Windows**：走 IOCP，真 proactor。发起操作时把 OVERLAPPED 结构和缓冲区
  一起交给内核，完成端口队列里出来的是做完的事；
- **POSIX**：epoll/kqueue/select 本质都是 reactor，asio 用它们**模拟**
  proactor——登记就绪兴趣，poll 报告就绪，框架自己执行系统调用，再把结果
  包装成「完成」投递给 handler。模拟的关键在最后一步：让用户看到的仍然只是
  「结果到了」，而不是「可以动手了」。

sasio 在 Windows 上偏偏不用 IOCP，选了模拟路线，用 WSAPoll 当 reactor。
原因很实际：IOCP 的完成端口、OVERLAPPED、`GetQueuedCompletionStatus` 会把模型
埋进一整套 Win32 机制里，读的人要先爬过 Windows 才能看见异步；WSAPoll 版本的
每一行都停在 socket 语义上，并且和 asio 的 POSIX 后端（`epoll_reactor` /
`select_reactor`）结构同构——分层与职责一一对应，只是代码量差一个数量级。
代价是这本书少讲一个真 proactor——第 14 章的阅读地图里给了
`win_iocp_io_context` 的补课路径。

无论哪张脸，asio 暴露给你的 API 是同一个：`async_read_some` 在两种平台上的
行为没有区别。这个「API 不变、脸在变」的事实，反过来说明了**引擎契约**可以
独立于平台存在——下面这三句就是全书的地基。

## 引擎契约（全书三条不变量）

**契约一：发起函数绝不内联执行 handler。** I/O 完成（read/write/connect/
accept/resolve）一律经 `scheduler::post` 投递到队列；到期定时器由 scheduler 在
run 循环里直接调用（也不在发起函数里）；`dispatch` 是仅有的就地入口，两条
dispatch 路径（scheduler 与 strand，见第 10 章）共用同一判据：**调用线程已在
`run()` 里，且目标当前空闲**——scheduler 看的是 context，strand 看的是 strand
自身。asio 把这两个判据写成 `can_dispatch()` 与 `can_dispatch() && !locked_`，
sasio 的实现与之一一同构。发起函数做的事只有一件：把「就绪之后做什么」
登记下来。

**契约二：handler 永远在 `run()` 里执行。** 发起函数返回时，你的程序原路
继续；结果在未来的某个时刻，被某个调用 `run()` 的线程交付。没有 `run()`，
就没有任何 handler 会执行——哪怕数据早就到了。sasio 的每一条投递路径都遵守
这一点，strand 也不例外（第 10 章）。初学者最常见的坑就在这：post 之后忘了
`run()`，代码安静地什么都不做。

**契约三：通知只有三个来源。** handler 队列（`post` 进来的）、就绪描述符
（reactor 报告的）、到期定时器（堆顶到点）。引擎的每圈循环都把三个来源各看
一遍，谁有事处理谁。多线程竞争、唤醒、停止规则，全部围绕这三个来源展开。

把这三句记牢。第 4 章的 work 计数、第 5 章的唤醒、第 6 章的取消，都是在这三句
地基上盖的房子；到时候任何一处行为让你意外，回来重读契约，多半能自己推出来。

## 一次 `async_read_some` 的一生

看最常用的一条链路。单线程，socket 已连接，对端随时可能发数据：

```text
 user code                socket                  reactor                scheduler
    | async_read_some(buf,h)  |                       |                       |
    |------------------------->| store buf/h, register |                       |
    |                         | read interest         |                       |
    |                         |---------------------->| start_read(fd)        |
    |   returns (nothing happened yet)                 |                       |
    |<------------------------|                        |                       |
    |                         |                        |  WSAPoll(all fds)     |
    |                         |                        |== peer data arrives ==|
    |                         |  do_read(): recv()     |                       |
    |                         |<----------------------| invokes the registered |
    |                         |  got n bytes           | callback              |
    |                         |-------- post(h(n,ec)) --------->               | enqueue
    |                         |                        |                       |
    |     h(ec, n) runs inside the run() loop           |                       |
    |<--------------------------------------------------------- ------------|

```

这条链上有三次交接——单线程程序，是三个时间点，不是三次线程切换：

1. **发起**：`async_read_some` 把缓冲区、handler 存进 socket 对象，向 reactor
   登记这个描述符的读就绪兴趣，然后返回。它没碰任何数据——此时对端可能
   还没发任何东西；
2. **就绪**：`run()` 循环阻塞在 `WSAPoll` 里。对端数据到达，poll 返回，reactor
   找到这个描述符登记的回调并调用它——这个回调不是你的 handler，是 socket
   自己的 `do_read`；
3. **投递**：`do_read` 执行真正的 `recv`，拿到字节数，把你的 handler 连同结果
   `post` 进 scheduler 的队列。run 循环下一圈把它取出来执行。

注意第 2 步和第 3 步的分工：**reactor 只负责「叫醒」，真正做 I/O 的是 socket 的
`do_read`**。这就是「用 reactor 模拟 proactor」的桥——叫醒之后补上系统调用，
用户拿到的就还是完成通知。契约一在这条链上被检验了两次：发起时没执行 handler，
`do_read` 之后也没执行（只是 post）——你的 handler 永远只出现在第 4 步。

多线程 `run()` 会在这条链上引入竞争：两个线程都睡在 `WSAPoll` 里，谁该被唤醒、
就绪会不会被重复投递——这是第 5 章的主题，也是最精彩的 bug 富矿。

## 精读：引擎的循环骨架

上面那条链路最终汇进同一个循环。`src/scheduler.cpp` 的 `run_one_impl`
（83 行）是全库最重要的一段代码，这里先看骨架——去掉锁和计数，留下形状：

```cpp
for (;;)
{
  if (stopped())                        // someone called stop(): quit now
    return false;

  if (!queue_.empty())                  // source 1: the handler queue
    { pop one and invoke it; return true; }

  if (earliest timer expired)           // source 2: the timer heap
    { pop and invoke its handler; return true; }

  timeout = next timer expiry;          // source 3 decides how long we may sleep
  reactor_.run_for(timeout);            // sleep in WSAPoll until any source fires
}

```

三行各对应一个契约的落点：`return true` 之前的那次执行就是契约二（一切都在
run 循环里）；来源一和来源二的分道——队列里的 handler 是「别人 post 的」，
定时器的 handler 是 scheduler 直接调的——就是契约一里那句「不经队列」；末尾
两行是契约三的第三来源，也是唯一会阻塞的一行。

一个细节值得单独说：`run_for` 返回后，控制流回到循环顶，**重新看一遍队列**。
为什么？因为睡在 `WSAPoll` 里的这段时间，任何线程都可能 `post` 了新 handler——
不回去看就会漏。整个引擎的形状就是这一句：**每圈都把三个来源重新检查一遍**。
第 5 章会讲，为了「别的线程 post 之后睡着的线程能被叫醒」，asio 和 sasio 各付了
什么代价；眼下你只需要记住这个形状。

骨架里还有三处被我们抹掉的细节——work 计数怎么算「还有没有活干」、
`stopped()` 之后未执行的 handler 去哪了、锁以什么顺序拿才不会死锁——全部留到
第 4 章精读全文时补上。

## 对照表

| 问题 | asio（Windows 脸） | asio（POSIX 脸） | sasio |
| --- | --- | --- | --- |
| 等待机制 | IOCP 完成端口 | epoll / kqueue / select | WSAPoll |
| 谁做 I/O | 内核（真 proactor） | 框架就绪后自己做 | 框架就绪后自己做 |
| 源码入口 | `detail/win_iocp_io_context.hpp` | `detail/epoll_reactor.hpp`、`detail/select_reactor.hpp` | `src/reactor.cpp` |
| 定时器驱动 | 完成端口等待超时 | reactor 算 poll 超时 | scheduler 算 poll 超时 |
| 对应关系 | —— | 与 sasio 同构 | —— |

一句话：**sasio 选了 asio 的 POSIX 路线，把它跑在 Windows 上**。读 sasio 的
reactor，就是在读 asio `epoll_reactor` 的缩小版；读完之后去 POSIX 脸的 asio
源码，模型不需要换脑子。真 proactor（IOCP 脸）的差异留给第 14 章。

## 练习

**实验**：构建并运行 echo 服务器：

```bat
cmake --build build --target ex03_echo_server
build\ex03_echo_server.exe 5555

```

可执行文件在 `build\` 下；如果第 1 章用了默认的 Visual Studio 生成器，
路径是 `build\Debug\ex03_echo_server.exe`。

另开两个终端，各用 `telnet 127.0.0.1 5555` 连上，交替敲字符，两边都能收到回声。
单个 `run()` 线程同时在伺候两个连接——对照本章的时序图解释它：两个 socket 登记
在同一个描述符表里，`WSAPoll` 一次报两个就绪，谁就绪处理谁。试着让一个连接
只连不发：另一个的回声不受影响，因为前者的描述符没有就绪，循环不会在它身上
浪费一次系统调用。

**bug 狩猎**：打开 `tests/asio/steady_timer.cpp`，找到 cancel 相关用例：
先 `expires_after` + `async_wait`，再 `cancel()`。`cancel()` 的返回值是 1
（取消了 1 个），但 handler **仍然会被调用**——带着 `operation_aborted` 错误。
先预测两件事：handler 在哪个时刻被调用？如果在 `cancel()` 的调用栈里同步调用，
会有什么问题？再运行验证。「取消不等于没有通知」是第 6 章的核心契约，它的原因
（避免重入）此刻你已经可以猜到了。

---

模型立住了：发起只登记、完成在 `run()` 里、通知来自三个来源。但「发起只登记」
有个前提——得有个地方登记。socket 的状态存哪、reactor 和定时器堆谁持有、
为什么 `io_context` 自己几乎一行逻辑都没有？下一章：service 注册表。
