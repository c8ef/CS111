#pragma once

#include <functional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Throw an exception based on the current POSIX error number errno.
[[noreturn]] inline void threrror(const char *msg) {
  throw std::system_error(errno, std::system_category(), msg);
}

// Split a path into the directory and filename components.
std::pair<std::string, std::string> splitpath(const char *path);

// Split a path into its path components.
std::vector<std::string> path_components(const std::string &s);

// A file descriptor that closes itself on destruction
struct unique_fd {
  int fd_;
  unique_fd() : fd_(-1) {}
  explicit unique_fd(int fd) : fd_(fd) {}
  unique_fd(unique_fd &&other) : fd_(other.release()) {}
  ~unique_fd() { close(); }
  unique_fd &operator=(unique_fd &&other) {
    close();
    fd_ = other.release();
    return *this;
  }
  int release() {
    int old = fd_;
    fd_ = -1;
    return old;
  }
  void set(int fd) {
    close();
    fd_ = fd;
  }
  operator int() const { return fd_; }
  // Remember to compare to -1 and not say if (fd) ...
  explicit operator bool() const = delete;

private:
  void close();
};

struct cleanup {
  std::function<void()> f_ = nullptr;
  cleanup() = default;
  cleanup(cleanup &&c) : f_(c.f_) { c.f_ = nullptr; }
  template <typename F> cleanup(F &&f) : f_(std::forward<F>(f)) {}
  cleanup &operator=(const cleanup &c) = delete;
  cleanup &operator=(std::function<void()> f) {
    f_ = std::move(f);
    return *this;
  }
  ~cleanup() {
    if (f_)
      f_();
  }
};
