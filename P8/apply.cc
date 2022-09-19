#include <iostream>

#include "replay.hh"
#include "v6fs.hh"

FScache cache;

void apply_log(V6FS &fs) {
  V6Replay r(fs);
  r.replay();
}

int main(int argc, char **argv) {
  auto [dir, prog] = splitpath(argv[0]);
  if (argc != 2) {
    fprintf(stderr, "usage: %s <fs-image>\n", prog.c_str());
    exit(1);
  }

  std::unique_ptr<V6FS> fsp;
  try {
    fsp = std::make_unique<V6FS>(argv[1], cache, V6FS::V6_NOLOG);
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    exit(1);
  }

  apply_log(*fsp);
}
