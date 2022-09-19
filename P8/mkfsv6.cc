#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "layout.hh"
#include "v6fs.hh"

const char *progname;
FScache cache(30);

[[noreturn]] static void usage() {
  fprintf(stderr, "usage: %s file.img [#sectors [#inodes [#journal-blocks]]\n",
          progname);
  exit(1);
}

bool create_file(const char *target, int nblocks, int ninodes) {
  int fd = open(target, O_CREAT | O_EXCL | O_WRONLY, 0666);
  if (fd < 0) {
    perror(target);
    return false;
  }

  ftruncate(fd, nblocks * SECTOR_SIZE);

  filsys s;
  memset(&s, 0, sizeof(s));
  s.s_isize = (ninodes + INODES_PER_BLOCK - 1) / INODES_PER_BLOCK;
  s.s_fsize = nblocks;
  uint32_t now = time(NULL);
  s.s_time[0] = now >> 16;
  s.s_time[1] = now;
  pwrite(fd, &s, sizeof(s), SUPERBLOCK_SECTOR * SECTOR_SIZE);

  uint16_t magic = BOOTBLOCK_MAGIC_NUM;
  pwrite(fd, &magic, sizeof(magic), 0);

  close(fd);
  return true;
}

int main(int argc, char **argv) {
  if (argc == 0)
    progname = "mkfsv6";
  else if ((progname = std::strrchr(argv[0], '/')))
    ++progname;
  else
    progname = argv[0];

  if (argc < 2 || argc > 5)
    usage();

  const char *target = argv[1];

  int nblocks = 0xffff;
  if (argc >= 3) {
    nblocks = atoi(argv[2]);
    if (nblocks <= 0)
      usage();
    if (nblocks > 0xffff)
      nblocks = 0xffff;
  }

  int ninodes = nblocks / 4;
  if (argc >= 4) {
    ninodes = atoi(argv[3]);
    if (ninodes < 1)
      usage();
    if (ninodes > nblocks)
      ninodes = nblocks;
  }

  int log_blocks = -1;
  if (argc >= 5)
    log_blocks = atoi(argv[4]);

  if (!create_file(target, nblocks, ninodes))
    exit(1);

  V6FS fs(target, cache);
  const uint16_t start = INODE_START_SECTOR + fs.superblock().s_isize;
  for (uint16_t bn = nblocks; bn-- > start;)
    fs.bfree(bn);

  Ref<Inode> ip = fs.iget(ROOT_INUMBER);
  Ref<Buffer> bp = fs.balloc(true);

  ip->i_mode = IALLOC | IFDIR | 0755;
  ip->i_nlink = 2;
  ip->i_addr[0] = bp->blockno();
  ip->mtouch();
  ip->atouch();

  ip->create(".").set_inum(ROOT_INUMBER);
  ip->create("..").set_inum(ROOT_INUMBER);

  if (log_blocks != -1)
    V6Log::create(fs, log_blocks);
  return 0;
}
