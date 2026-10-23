cmake_minimum_required(VERSION 3.20)
project(sasio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sasio STATIC
  src/execution_context.cpp
  src/io_context.cpp
  src/scheduler.cpp
  src/reactor.cpp
  src/timer_queue.cpp
  src/steady_timer.cpp
  src/socket_ops.cpp
  src/ip_tcp.cpp
  src/strand.cpp
)

target_include_directories(sasio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(sasio PUBLIC _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(sasio PUBLIC ws2_32)

if (MSVC)
  target_compile_options(sasio PRIVATE /W4 /permissive-)
endif()

add_executable(ex01_post examples/ex01_post.cpp)
target_link_libraries(ex01_post PRIVATE sasio)
if (MSVC)
  target_compile_options(ex01_post PRIVATE /W4 /permissive-)
endif()

add_executable(ex02_timer examples/ex02_timer.cpp)
target_link_libraries(ex02_timer PRIVATE sasio)
if (MSVC)
  target_compile_options(ex02_timer PRIVATE /W4 /permissive-)
endif()

add_executable(ex03_echo_server examples/ex03_echo_server.cpp)
target_link_libraries(ex03_echo_server PRIVATE sasio)
if (MSVC)
  target_compile_options(ex03_echo_server PRIVATE /W4 /permissive-)
endif()

enable_testing()

# The test sources are adapted from asio's own test suite; see tests/asio/.
add_executable(test_io_context tests/asio/io_context.cpp)
target_include_directories(test_io_context PRIVATE tests/asio)
target_link_libraries(test_io_context PRIVATE sasio)

add_executable(test_steady_timer tests/asio/steady_timer.cpp)
target_include_directories(test_steady_timer PRIVATE tests/asio)
target_link_libraries(test_steady_timer PRIVATE sasio)

add_executable(test_ip_tcp tests/asio/ip_tcp.cpp)
target_include_directories(test_ip_tcp PRIVATE tests/asio)
target_link_libraries(test_ip_tcp PRIVATE sasio)

add_executable(test_strand tests/asio/strand.cpp)
target_include_directories(test_strand PRIVATE tests/asio)
target_link_libraries(test_strand PRIVATE sasio)

add_executable(test_read tests/asio/read.cpp)
target_include_directories(test_read PRIVATE tests/asio)
target_link_libraries(test_read PRIVATE sasio)

add_executable(test_write tests/asio/write.cpp)
target_include_directories(test_write PRIVATE tests/asio)
target_link_libraries(test_write PRIVATE sasio)

if (MSVC)
  target_compile_options(test_io_context PRIVATE /W4 /permissive-)
  target_compile_options(test_steady_timer PRIVATE /W4 /permissive-)
  target_compile_options(test_ip_tcp PRIVATE /W4 /permissive-)
  target_compile_options(test_strand PRIVATE /W4 /permissive-)
  target_compile_options(test_read PRIVATE /W4 /permissive-)
  target_compile_options(test_write PRIVATE /W4 /permissive-)
endif()

add_test(NAME asio_io_context COMMAND test_io_context)
add_test(NAME asio_steady_timer COMMAND test_steady_timer)
add_test(NAME asio_ip_tcp COMMAND test_ip_tcp)
add_test(NAME asio_strand COMMAND test_strand)
add_test(NAME asio_read COMMAND test_read)
add_test(NAME asio_write COMMAND test_write)
