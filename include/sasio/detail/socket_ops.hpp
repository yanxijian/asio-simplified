//
// socket_ops.hpp
// ~~~~~~~~~~~~~~
//
// Thin wrappers around the Winsock calls used by socket and acceptor. Keeping
// them in one place mirrors asio/detail/socket_ops.hpp, where every platform
// difference is hidden behind this layer; here there is only Windows, so the
// layer mainly serves readability.
//
#ifndef SASIO_DETAIL_SOCKET_OPS_HPP
#define SASIO_DETAIL_SOCKET_OPS_HPP

#include "sasio/config.hpp"
#include "sasio/error.hpp"

#include <cstddef>

namespace sasio {
namespace detail {
namespace socket_ops {

SOCKET create_stream_socket(error_code& ec);
SOCKET create_datagram_socket(error_code& ec);
bool set_non_blocking(SOCKET descriptor, error_code& ec);
bool set_reuse_address(SOCKET descriptor, bool value, error_code& ec);

bool bind_socket(SOCKET descriptor, const sockaddr_in& address,
    error_code& ec);
bool listen_socket(SOCKET descriptor, int backlog, error_code& ec);
bool connect_socket(SOCKET descriptor, const sockaddr_in& address,
    error_code& ec);

SOCKET accept_socket(SOCKET descriptor, sockaddr_in& address, error_code& ec);

// Return the number of bytes transferred, or -1 with ec set. A would_block
// result means "not ready yet": register interest and try again later.
int recv_socket(SOCKET descriptor, void* data, std::size_t size,
    error_code& ec);
int send_socket(SOCKET descriptor, const void* data, std::size_t size,
    error_code& ec);
int recv_from_socket(SOCKET descriptor, void* data, std::size_t size,
    sockaddr_in& address, error_code& ec);
int send_to_socket(SOCKET descriptor, const void* data, std::size_t size,
    const sockaddr_in& address, error_code& ec);

bool shutdown_socket(SOCKET descriptor, int how, error_code& ec);

// Reads the pending socket error (SO_ERROR). Used after a non-blocking connect
// reports writable readiness.
bool get_socket_error(SOCKET descriptor, error_code& ec);

bool local_endpoint(SOCKET descriptor, sockaddr_in& address, error_code& ec);
bool remote_endpoint(SOCKET descriptor, sockaddr_in& address, error_code& ec);

void close_socket(SOCKET descriptor);

} // namespace socket_ops
} // namespace detail
} // namespace sasio

#endif // SASIO_DETAIL_SOCKET_OPS_HPP
