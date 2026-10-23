#include <sasio/buffer.hpp>
#include <sasio/io_context.hpp>
#include <sasio/ip/tcp.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

// Chapter 2, example: an asynchronous TCP echo server.
//
// Adapted from asio's src/examples/cpp11/echo/async_tcp_echo_server.cpp,
// with two differences caused by the simplified API:
//   * async_accept() fills a caller-provided socket instead of returning one;
//   * chapter 2 has no composed async_write(), so the session loops over
//     async_write_some() until the buffer is drained.
//
// Try it with: ex03_echo_server 5555
// and connect with any TCP client (telnet, netcat, ...).

using sasio::error_code;
namespace tcp = sasio::ip::tcp;

class session : public std::enable_shared_from_this<session>
{
public:
  explicit session(sasio::io_context& ctx) : socket_(ctx) {}

  tcp::socket& socket() { return socket_; }

  void start() { do_read(); }

private:
  void do_read()
  {
    auto self = shared_from_this();
    socket_.async_read_some(sasio::buffer(data_, max_length),
        [this, self](error_code ec, std::size_t length) {
          if (ec)
          {
            std::printf("session closed: %s\n", ec.message().c_str());
            return;
          }

          // recv() reports EOF as a successful zero-byte read.
          if (length == 0)
            return;

          do_write(length);
        });
  }

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
            do_write_some();
          else
            do_read();
        });
  }

  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
  std::size_t write_offset_ = 0;
  std::size_t write_size_ = 0;
};

class server
{
public:
  server(sasio::io_context& ctx, unsigned short port)
    : context_(ctx),
      acceptor_(ctx, tcp::endpoint(port))
  {
    std::printf("listening on %s\n",
        acceptor_.local_endpoint().to_string().c_str());
    do_accept();
  }

private:
  void do_accept()
  {
    auto next = std::make_shared<session>(context_);
    acceptor_.async_accept(next->socket(),
        [this, next](error_code ec) {
          if (!ec)
            next->start();
          do_accept();
        });
  }

  sasio::io_context& context_;
  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  try
  {
    const unsigned short port = argc > 1
        ? static_cast<unsigned short>(std::atoi(argv[1]))
        : static_cast<unsigned short>(5555);

    sasio::io_context ctx;
    server srv(ctx, port);
    ctx.run();
  }
  catch (const std::exception& e)
  {
    std::fprintf(stderr, "exception: %s\n", e.what());
    return 1;
  }

  return 0;
}
