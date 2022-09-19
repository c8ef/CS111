#include <unistd.h>

#include <cassert>
#include <cstring>

#include "bufio.hh"
#include "util.hh"

namespace {

inline uint32_t offset(uint32_t pos) { return pos % BUF_SIZE; }

inline uint32_t lower_bound(uint32_t pos) { return pos - offset(pos); }

inline uint32_t upper_bound(uint32_t pos) {
  return lower_bound(pos) + BUF_SIZE;
}

} // anonymous namespace

// Note that the beginning of FdReader::buf_ and the end of
// FdWriter::buf_ always correspond to a file offset with BUF_SIZE
// alignment.

// FdReader::buf_ contains bytes in the half-open interval:
//   [lower_bound(pos_), buf_end_)

bool FdReader::tryread(void *_data, size_t len) {
  char *data = static_cast<char *>(_data);
  while (len > 0) {
    if (pos_ >= buf_end_) {
      uint32_t start = lower_bound(pos_);
      if (int n = ::pread(fd_, buf_, BUF_SIZE, start); n == -1)
        threrror("pread");
      else if (n <= int(offset(pos_)))
        return false;
      else
        buf_end_ = start + n;
    }
    int n = std::min<uint32_t>(buf_end_ - pos_, len);
    memcpy(data, buf_ + offset(pos_), n);
    pos_ += n;
    data += n;
    len -= n;
  }
  return true;
}

void FdReader::seek(uint32_t pos) {
  if (pos < lower_bound(pos_) || buf_end_ <= pos)
    flush();
  pos_ = pos;
}

// FdWriter invariants:
//   * buf_start_ <= pos_
//   * upper_bound(pos_) == upper_bound(buf_start_)
// These imply:  buf_start_ <= pos_ < upper_bound(buf_start_)

void FdWriter::write(const void *_data, std::size_t len) {
  const char *data = static_cast<const char *>(_data);
  while (len > 0) {
    uint32_t n = std::min<uint32_t>(upper_bound(buf_start_) - pos_, len);
    std::memcpy(buf_ + (pos_ - buf_start_), data, n);
    pos_ += n;
    data += n;
    len -= n;
    if (!offset(pos_))
      flush();
  }
}

void FdWriter::flush() {
  if (pos_ <= buf_start_)
    return;
  int len = pos_ - buf_start_;
  if (::pwrite(fd_, buf_, len, buf_start_) != len)
    threrror("pwrite");
  buf_start_ = pos_;
}

void FdWriter::seek(uint32_t pos) {
  flush();
  pos_ = buf_start_ = pos;
}
