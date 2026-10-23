//
// config.hpp
// ~~~~~~~~~~
//
// The one place where the target platform is chosen. Compare with
// asio/detail/config.hpp, which carries a branch for every platform Asio
// supports (Windows, Linux, macOS, BSD, Solaris, ...). We support exactly one.
//
#ifndef SASIO_CONFIG_HPP
#define SASIO_CONFIG_HPP

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
# define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

// asio uses ASIO_WINDOWS, ASIO_HAS_IOCP and friends for the same purpose.
#define SASIO_WINDOWS 1

#endif // SASIO_CONFIG_HPP
