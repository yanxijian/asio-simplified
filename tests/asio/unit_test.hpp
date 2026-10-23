//
// unit_test.hpp
// ~~~~~~~~~~~~~
//
// Adapted from asio/src/tests/unit/unit_test.hpp
// (Copyright (c) 2003-2026 Christopher M. Kohlhoff,
//  distributed under the Boost Software License 1.0).
//
// The macro names (ASIO_CHECK, ASIO_TEST_SUITE, ASIO_TEST_CASE) are kept
// unchanged so that the ported test files stay diffable against asio's own.
// Changes: namespace asio -> sasio, std::atomic counter instead of
// asio::detail::atomic_count, and the platform-specific (Sun/Borland) blocks
// removed.
//
#ifndef SASIO_TEST_UNIT_TEST_HPP
#define SASIO_TEST_UNIT_TEST_HPP

#include <atomic>
#include <cstdlib>
#include <iostream>

#if defined(_DEBUG) && defined(_MSC_VER)
# include <crtdbg.h>
# include <cstdio>
#endif

#define ASIO_TEST_IOSTREAM std::cerr

namespace sasio {
namespace detail {

// Debug CRT assertions normally open a dialog with an "Ignore" button. Ignoring
// one would let the test continue after an undefined operation and still print
// "passed". Turn every assertion into an immediate abort with the message on
// stderr instead: no dialog, no way to continue, and CTest sees a failure.
#if defined(_DEBUG) && defined(_MSC_VER)
inline int report_assert_and_abort(int report_type, char* message, int*)
{
  if (report_type == _CRT_ASSERT || report_type == _CRT_ERROR)
  {
    const char* kind = (report_type == _CRT_ASSERT) ? "assertion" : "error";
    std::fprintf(stderr, "\n*** CRT %s: %s\n", kind,
        message ? message : "(no message)");
    std::fflush(stderr);
    // _Exit rather than abort(): abort() is itself reported through this hook,
    // which would recurse. _Exit also skips atexit/static destructors, which is
    // what we want when an invariant has already been violated.
    std::_Exit(3);
  }
  return 0;
}

inline void install_assert_handler()
{
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, &report_assert_and_abort);
}
#else
inline void install_assert_handler()
{
}
#endif

inline const char*& test_name()
{
  static const char* name = 0;
  return name;
}

inline std::atomic<long>& test_errors()
{
  static std::atomic<long> errors(0);
  return errors;
}

inline void begin_test_suite(const char* name)
{
  install_assert_handler();
  test_name();
  test_errors();
  ASIO_TEST_IOSTREAM << name << " test suite begins" << std::endl;
}

inline int end_test_suite(const char* name)
{
  ASIO_TEST_IOSTREAM << name << " test suite ends" << std::endl;
  ASIO_TEST_IOSTREAM << "\n*** ";
  const long errors = test_errors();
  if (errors == 0)
    ASIO_TEST_IOSTREAM << "No errors detected.";
  else if (errors == 1)
    ASIO_TEST_IOSTREAM << "1 error detected.";
  else
    ASIO_TEST_IOSTREAM << errors << " errors detected.";
  ASIO_TEST_IOSTREAM << std::endl;
  return errors == 0 ? 0 : 1;
}

template <void (*Test)()>
inline void run_test(const char* name)
{
  test_name() = name;
  const long errors_before = test_errors();
  Test();
  if (test_errors() == errors_before)
    ASIO_TEST_IOSTREAM << name << " passed" << std::endl;
  else
    ASIO_TEST_IOSTREAM << name << " failed" << std::endl;
}

inline void null_test()
{
}

} // namespace detail
} // namespace sasio

#define ASIO_CHECK(expr) \
  do { if (!(expr)) { \
    ASIO_TEST_IOSTREAM << __FILE__ << "(" << __LINE__ << "): " \
      << sasio::detail::test_name() << ": " \
      << "check '" << #expr << "' failed" << std::endl; \
    ++sasio::detail::test_errors(); \
  } } while (0)

#define ASIO_CHECK_MESSAGE(expr, msg) \
  do { if (!(expr)) { \
    ASIO_TEST_IOSTREAM << __FILE__ << "(" << __LINE__ << "): " \
      << sasio::detail::test_name() << ": " \
      << msg << std::endl; \
    ++sasio::detail::test_errors(); \
  } } while (0)

#define ASIO_ERROR(msg) \
  do { \
    ASIO_TEST_IOSTREAM << __FILE__ << "(" << __LINE__ << "): " \
      << sasio::detail::test_name() << ": " \
      << msg << std::endl; \
    ++sasio::detail::test_errors(); \
  } while (0)

#define ASIO_TEST_SUITE(name, tests) \
  int main() \
  { \
    sasio::detail::begin_test_suite(name); \
    tests \
    return sasio::detail::end_test_suite(name); \
  }

#define ASIO_TEST_CASE(test) \
  sasio::detail::run_test<&test>(#test);

#endif // SASIO_TEST_UNIT_TEST_HPP
