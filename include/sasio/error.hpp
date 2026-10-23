//
// error.hpp
// ~~~~~~~~~
//
// asio has its own error_code type because it still supports compilers and
// configurations where std::error_code is not usable. As a simplification we
// use std::error_code directly, but keep the same vocabulary:
//
//   sasio::error_code        ->  asio::error_code
//   sasio::operation_aborted ->  asio::error::operation_aborted
//
#ifndef SASIO_ERROR_HPP
#define SASIO_ERROR_HPP

#include "config.hpp"

#include <system_error>

namespace sasio {

using error_code = std::error_code;

// Every operation that is cancelled (timer::cancel(), socket::close(), ...)
// completes with this error, exactly like asio::error::operation_aborted.
inline const error_code operation_aborted =
    std::make_error_code(std::errc::operation_canceled);

// Would the operation block? On a non-blocking socket this is the normal
// answer from recv/send/accept/connect and simply means "register interest in
// readiness and try again later". asio spells it asio::error::would_block.
inline const error_code would_block =
    std::make_error_code(std::errc::operation_would_block);

// Returned when an asynchronous operation is started while an operation for
// the same direction is already outstanding.
inline const error_code already_started =
    std::make_error_code(std::errc::operation_in_progress);

// Returned when a socket operation is attempted on a closed socket.
inline const error_code bad_descriptor =
    std::make_error_code(std::errc::bad_file_descriptor);

// Winsock reports Win32 error numbers, so they belong to the system category.
// asio hides the same mapping inside asio::error::get_system_category().
inline error_code last_socket_error()
{
  return error_code(::WSAGetLastError(), std::system_category());
}

} // namespace sasio

#endif // SASIO_ERROR_HPP
