#include <cstring>
#include <stdio.h>
#include <unistd.h>

#include "util.hh"

std::pair<std::string, std::string> splitpath(const char *path) {
  const char *p = std::strrchr(path, '/');
  if (!p)
    return {".", path};
  const char *tail = p + 1;
  while (p > path + 1 && p[-1] == '/')
    --p;
  return {{path, p}, *tail ? tail : "."};
}

std::vector<std::string> path_components(const std::string &s) {
  std::vector<std::string> ret;
  for (std::string::size_type p = 0;;) {
    if (p = s.find_first_not_of('/', p); p == s.npos)
      return ret;
    std::string::size_type e = s.find_first_of('/', p);
    if (e == s.npos)
      e = s.size();
    std::string_view sv(s.data() + p, e - p);
    if (sv == ".." && !ret.empty())
      ret.pop_back();
    else if (sv != ".")
      ret.emplace_back(sv);
    p = e;
  }
}

void unique_fd::close() {
  if (fd_ != -1 && ::close(fd_) == -1)
    perror("close");
}
