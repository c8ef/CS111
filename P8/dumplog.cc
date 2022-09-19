#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include "layout.hh"
#include "log.hh"

using namespace std;

struct closefd {
  int fd = -1;
  ~closefd() { close(fd); }
};

void read_log(const char *image, int startpos) try {
  int fd = open(image, O_RDWR);
  if (fd == -1)
    threrror(image);

  filsys fs;
  if (pread(fd, &fs, sizeof(fs), SUPERBLOCK_SECTOR * SECTOR_SIZE) !=
      sizeof(fs)) {
    fprintf(stderr, "can't read superblock\n");
    exit(1);
  }
  loghdr lh;
  read_loghdr(fd, &lh, fs.s_fsize);

  FdReader f(fd);
  if (startpos < 0)
    f.seek(lh.l_checkpoint);
  else if (size_t(startpos) <= lh.logstart() * SECTOR_SIZE)
    f.seek(lh.logstart() * SECTOR_SIZE);
  else
    f.seek(startpos);

  bool above = true;
  uint32_t pos = f.tell();
  while (above || pos < lh.l_checkpoint) {
    LogEntry le;
    printf("[offset %u]\n", f.tell());
    le.load(f);
    puts(le.show(&fs).c_str());
    uint32_t newpos = f.tell();
    if (newpos < pos)
      above = false;
    pos = newpos;
  }
} catch (log_corrupt &e) {
  printf("* Exiting because: %s\n", e.what());
  exit(0);
}

int main(int argc, char **argv) {
  auto [dir, prog] = splitpath(argv[0]);
  int startpos = 0;
  if (argc == 3) {
    if (*argv[2] == 'c')
      startpos = -1; // start from checkpoint
    else
      startpos = atoi(argv[2]);
  } else if (argc != 2) {
    fprintf(stderr, "usage: %s <fs-image> [<offset> | c]\n", prog.c_str());
    exit(1);
  }
  read_log(argv[1], startpos);
}
