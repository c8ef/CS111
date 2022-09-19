#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <mountpoint>\n", argv[0]);
    exit(1);
  }
  if (!isatty(0)) {
    signal(SIGHUP, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    char c;
    read(0, &c, 1);
  }
  close(2);
  open("/dev/null", O_WRONLY);
  execlp("fusermount", "fusermount", "-zu", argv[1], nullptr);
  exit(1);
}
