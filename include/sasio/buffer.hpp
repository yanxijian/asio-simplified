//
// buffer.hpp
// ~~~~~~~~~~
//
// asio has a whole buffer library (mutable_buffer, const_buffer, buffer
// sequences, dynamic buffers). Chapter 2 needs exactly two things: a pointer
// plus a length, and helper functions to build them. The names follow asio.
//
// asio counterpart: asio/buffer.hpp
//
#ifndef SASIO_BUFFER_HPP
#define SASIO_BUFFER_HPP

#include <cstddef>

namespace sasio {

class mutable_buffer
{
public:
  mutable_buffer() : data_(nullptr), size_(0) {}
  mutable_buffer(void* data, std::size_t size) : data_(data), size_(size) {}

  void* data() const { return data_; }
  std::size_t size() const { return size_; }

private:
  void* data_;
  std::size_t size_;
};

class const_buffer
{
public:
  const_buffer() : data_(nullptr), size_(0) {}
  const_buffer(const void* data, std::size_t size)
    : data_(data), size_(size) {}

  // asio allows a mutable buffer to be used where a const one is expected.
  const_buffer(const mutable_buffer& other)
    : data_(other.data()), size_(other.size()) {}

  const void* data() const { return data_; }
  std::size_t size() const { return size_; }

private:
  const void* data_;
  std::size_t size_;
};

inline mutable_buffer buffer(void* data, std::size_t size)
{
  return mutable_buffer(data, size);
}

inline const_buffer buffer(const void* data, std::size_t size)
{
  return const_buffer(data, size);
}

template <std::size_t N>
inline mutable_buffer buffer(char (&data)[N])
{
  return mutable_buffer(data, N);
}

template <std::size_t N>
inline const_buffer buffer(const char (&data)[N])
{
  return const_buffer(data, N);
}

} // namespace sasio

#endif // SASIO_BUFFER_HPP
