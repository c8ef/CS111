#pragma once

#include <cstdint>
#include <stdexcept>

using std::size_t;
using std::uint32_t;

struct Reader {
  // Returns true if it reads the full amount, false if it receives
  // EOF before reading enough data.
  virtual bool tryread(void *, size_t) = 0;
};

struct Writer {
  virtual void write(const void *, std::size_t) = 0;
};

constexpr size_t BUF_SIZE = 8192;

class FdReader : public Reader {
  uint32_t buf_end_ = 0;
  uint32_t pos_ = 0;
  char buf_[BUF_SIZE];

public:
  const int fd_;
  FdReader(int fd) : fd_(fd) {}
  FdReader(const FdReader &) = delete;
  FdReader &operator=(const FdReader &) = delete;
  bool tryread(void *dst, size_t len) override;
  void flush() { buf_end_ = 0; }
  void seek(uint32_t pos);
  uint32_t tell() const { return pos_; }
};

class FdWriter : public Writer {
  uint32_t buf_start_ = 0;
  uint32_t pos_ = 0;
  char buf_[BUF_SIZE];

public:
  const int fd_;
  FdWriter(int fd) : fd_(fd) {}
  FdWriter(const FdWriter &) = delete;
  FdWriter &operator=(const FdWriter &) = delete;
  ~FdWriter() { flush(); }
  void write(const void *, std::size_t) override;
  void flush();
  void seek(uint32_t pos);
  uint32_t tell() const { return pos_; }
};
