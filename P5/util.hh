#pragma once

#include <system_error>

#include <unistd.h>

// Throw an exception based on the current POSIX error number errno.
[[noreturn]] inline void threrror(const char *msg) {
  throw std::system_error(errno, std::system_category(), msg);
}

// A file descriptor that closes itself on destruction
struct unique_fd {
  int fd_;
  unique_fd() : fd_(-1) {}
  explicit unique_fd(int fd) : fd_(fd) {}
  unique_fd(unique_fd &&other) : fd_(other.release()) {}
  ~unique_fd() { clear(); }
  unique_fd &operator=(unique_fd &&other) {
    clear();
    fd_ = other.release();
    return *this;
  }
  void clear() {
    if (fd_ != -1 && close(fd_) == -1)
      threrror("close");
  }
  int release() {
    int old = fd_;
    fd_ = -1;
    return old;
  }
  void set(int fd) {
    clear();
    fd_ = fd;
  }
  operator int() const { return fd_; }
  // Remember to compare to -1 and not say if (fd) ...
  explicit operator bool() const = delete;
};
