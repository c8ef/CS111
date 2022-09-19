#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "blockpath.hh"
#include "fsops.hh"

using namespace std::string_literals;

const char *progname;

FScache cache;

static const char *fs_path() {
  if (const char *target = getenv("V6IMG"))
    return target;
  return "v6.img";
}

static V6FS &fs(int flags = V6FS::V6_NOLOG) {
  flags |= V6FS::V6_NOLOG;
  static std::unique_ptr<V6FS> fsp;
  if (!fsp) {
    const char *target = getenv("V6IMG");
    if (!target)
      target = "v6.img";
    fsp = std::make_unique<V6FS>(target, cache, flags);
    return *fsp;
  }
  return *fsp;
}

std::string fmttime(time_t val) {
  char tmbuf[30] = "error";
  std::strftime(tmbuf, sizeof(tmbuf), "%b %d %Y %H:%M:%S",
                std::localtime(&val));
  return tmbuf;
}

std::string lsline(const struct Inode *ip, bool use_atime = false) {
  std::ostringstream res;
  res << std::setw(5) << ip->inum() << " ";
  const uint16_t mode = ip->i_mode;
  switch (mode & IFMT) {
  case IFDIR:
    res << 'd';
    break;
  case IFCHR:
    res << 'c';
    break;
  case IFBLK:
    res << 'b';
    break;
  case 0:
    res << '-';
    break;
  default:
    res << '?';
    break;
  }
  res << ((mode & IREAD) ? 'r' : '-');
  res << ((mode & IWRITE) ? 'w' : '-');
  if (mode & ISUID)
    res << ((mode & IEXEC) ? 's' : 'S');
  else
    res << ((mode & IEXEC) ? 'x' : '-');
  res << ((mode & IREAD >> 3) ? 'r' : '-');
  res << ((mode & IWRITE >> 3) ? 'w' : '-');
  if (mode & ISGID)
    res << ((mode & IEXEC >> 3) ? 's' : 'S');
  else
    res << ((mode & IEXEC >> 3) ? 'x' : '-');
  res << ((mode & IREAD >> 6) ? 'r' : '-');
  res << ((mode & IWRITE >> 6) ? 'w' : '-');
  if (mode & ISVTX)
    res << ((mode & IEXEC >> 6) ? 't' : 'T');
  else
    res << ((mode & IEXEC >> 6) ? 'x' : '-');
  time_t ftime = use_atime ? ip->atime() : ip->mtime();
  res << " " << std::setw(3) << unsigned(ip->i_nlink) << " " << std::setw(3)
      << unsigned(ip->i_uid) << " " << std::setw(3) << unsigned(ip->i_gid)
      << " " << std::setw(8) << ip->size() << " " << fmttime(ftime) << "  ";
  return res.str();
}

void dump(Ref<Inode> i) {
  if (!(i->i_mode & IALLOC))
    return;

#if 0
    printf("inode %d:\n", i->inum());
#define dump(c, field) printf("  %s: %" #c "\n", #field, i->field)
    dump(o, i_mode);
    dump(d, i_nlink);
    dump(d, i_uid);
    dump(d, i_gid);
    dump(d, size());
    dump(u, i_atime);
    dump(u, i_mtime);
#undef dump
#endif

  if ((i->i_mode & IFDIR) != IFDIR)
    return;
  printf(">>> directory entries in inode %d\n", i->inum());
  Cursor c(i);
  while (auto *d = c.next<direntv6>()) {
    if (!d->d_inumber)
      continue;
    auto child = fs().iget(d->d_inumber);
    std::cout << lsline(child.get()) << d->name() << std::endl;
    if (d->name() != "." && d->name() != "..")
      dump(child);
  }
  c.pos_ = 0;

  printf("<<< exiting inode %d\n", i->inum());
}

void cmd_ls(int argc, char **argv) {
  fs(V6FS::V6_RDONLY);
  bool use_atime = false;
  for (int i = 0; i < argc; ++i) {
    if (i == 0 && argv[i] == "-a"s) {
      use_atime = true;
      continue;
    }
    auto ip = fs().namei(argv[i]);
    if (!ip)
      std::cerr << argv[i] << ": no such file or directory" << std::endl;
    else if ((ip->i_mode & IFMT) != IFDIR)
      std::cout << lsline(ip.get(), use_atime) << argv[i] << std::endl;
    else {
      ip->atouch();
      std::cout << argv[i] << ":" << std::endl;
      Cursor c(ip);
      while (auto *d = c.next<direntv6>()) {
        if (!d->d_inumber)
          continue;
        auto ep = fs().iget(d->d_inumber);
        std::cout << lsline(ep.get()) << d->name() << std::endl;
      }
    }
  }
}

void cmd_cat(int argc, char **argv) {
  fs(V6FS::V6_RDONLY);
  for (int i = 0; i < argc; ++i) {
    auto ip = fs().namei(argv[i]);
    if (!ip)
      std::cerr << argv[i] << ": no such file or directory" << std::endl;
    else if ((ip->i_mode & IFMT) != 0)
      std::cerr << argv[i] << ": not a regular file" << std::endl;
    else {
      Cursor c(ip);
      for (;;) {
        char buf[512];
        if (int n = c.read(buf, sizeof(buf)); n > 0)
          write(1, buf, n);
        else
          break;
      }
    }
  }
}

void cmd_stat(int argc, char **argv) {
  fs(V6FS::V6_RDONLY);
  for (int i = 0; i < argc; ++i) {
    Ref<Inode> ip;
    if (*argv[i] == '#')
      ip = fs().iget(atoi(argv[i] + 1));
    else
      ip = fs().namei(argv[i]);
    if (!ip) {
      std::cerr << argv[i] << ": no such file or directory" << std::endl;
      continue;
    }
    std::cout << lsline(ip.get()) << argv[i] << std::endl;
    printf("        ino: %d\n", ip->inum());
#define dump(c, field) printf("        %s: " #c "\n", #field, ip->field)
    dump(0 % o, i_mode);
    dump(% d, i_nlink);
    dump(% d, i_uid);
    dump(% d, i_gid);
    dump(% d, size());
#undef dump
    for (unsigned j = 0; j < IADDR_SIZE; ++j)
      printf("        i_addr[%d]: %d\n", j, ip->i_addr[j]);
    printf("        atime: %s\n", fmttime(ip->atime()).c_str());
    printf("        mtime: %s\n", fmttime(ip->mtime()).c_str());
  }
}

void cmd_truncate(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: truncate FILE length" << std::endl;
    return;
  }

  Ref<Inode> ip;
  if (*argv[0] == '#')
    ip = fs().iget(atoi(argv[0] + 1));
  else
    ip = fs().namei(argv[0]);
  if (!ip) {
    std::cerr << argv[0] << ": no such file or directory" << std::endl;
    return;
  }
  ip->truncate(atoi(argv[1]));
}

void cmd_block(int argc, char **argv) {
  fs(V6FS::V6_RDONLY);
  for (int i = 0; i < argc; ++i) {
    int bn;
    Ref<Buffer> bp;
    try {
      bn = std::stoi(argv[i]);
      if (bn < 0)
        throw std::runtime_error("invalid block number");
      bp = fs().bread(bn);
    } catch (const std::exception &e) {
      std::cerr << argv[i] << ": " << e.what() << std::endl;
      continue;
    }
    unsigned char *p = reinterpret_cast<unsigned char *>(bp->mem_);
    if (argc > 1)
      printf("Block %d:\n", bn);
    bool skipped = false;
    for (unsigned i = 0; i < SECTOR_SIZE; i += 16) {
      if (i > 0 && !memcmp(p + i - 16, p + i, 16)) {
        skipped = true;
        continue;
      }
      if (skipped) {
        skipped = false;
        printf("*\n");
      }
      printf("%3d", i);
      for (unsigned j = 0; j < 16; ++j) {
        if (!(j % 4))
          printf(" ");
        printf("%02x", p[i + j]);
      }
      printf("  >");
      for (unsigned j = 0; j < 16; ++j) {
        unsigned char c = p[i + j];
        putchar(c >= 0x20 && c < 0x7f ? c : ' ');
      }
      printf("<\n");
    }
    if (skipped)
      printf("*\n");
  }
}

void cmd_iblock(int argc, char **argv) {
  fs(V6FS::V6_RDONLY);
  for (int i = 0; i < argc; ++i) {
    int bn;
    Ref<Buffer> bp;
    try {
      bn = std::stoi(argv[i]);
      if (bn < 0 || bn >= fs().superblock().s_fsize)
        throw std::runtime_error("invalid block number");
      bp = fs().bread(bn);
    } catch (const std::exception &e) {
      std::cerr << argv[i] << ": " << e.what() << std::endl;
      continue;
    }
    BlockPtrArray ba(bp);
    if (argc > 1)
      printf("Indirect block %d:\n", bn);
    int stop = ba.size();
    while (stop > 0 && !ba.at(stop - 1))
      --stop;
    for (int j = 0; j < stop; ++j)
      printf("  %3d: %d\n", j, ba.at(j));
  }
}

void cmd_put(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: put FILE V6FILE" << std::endl;
    return;
  }

  unique_fd in{argv[0] == "-"s ? dup(0) : open(argv[0], O_RDONLY)};
  if (in == -1) {
    std::cerr << argv[0] << ": " << strerror(errno) << std::endl;
    return;
  }
  if (struct stat sb;
      fstat(in, &sb) == -1 || (sb.st_mode & S_IFMT) == S_IFDIR) {
    std::cerr << argv[0] << ": is a directory" << std::endl;
    return;
  }

  auto [dname, fname] = splitpath(argv[1]);
  if (fname == ".")
    fname = splitpath(argv[0]).second;
  Ref<Inode> dir = fs().namei(dname);
  if (!dir) {
    std::cerr << argv[1] << ": no such directory" << std::endl;
    return;
  }

  Dirent de = dir->create(fname);
  Ref<Inode> out;
  if (de.inum()) {
    out = fs().iget(de.inum());
    if ((out->i_mode & IFMT) != IFREG) {
      std::cerr << argv[1] << ": not a regular file" << std::endl;
      return;
    }
    out->truncate();
  } else {
    if (out = fs().ialloc(); !out) {
      std::cerr << "out of inodes" << std::endl;
      return;
    }
    out->i_mode = IALLOC | 0644;
    out->i_nlink = 1;
    out->mark_dirty();
    de.set_inum(out->inum());
  }

  char buf[SECTOR_SIZE];
  Cursor c(out);
  out->mtouch();
  for (;;) {
    int n = read(in, buf, sizeof(buf));
    if (n <= 0) {
      if (n < 0)
        std::cerr << argv[0] << ": " << strerror(errno) << std::endl;
      out->put();
      return;
    }
    c.write(buf, n);
  }
}

void cmd_unlink(int argc, char **argv) {
  for (int i = 0; i < argc; ++i) {
    auto [dname, fname] = splitpath(argv[i]);
    if (fname == "") {
      std::cerr << argv[i] << ": trailing slash not allowed" << std::endl;
      continue;
    }
    Ref<Inode> dir = fs().namei(dname);
    Dirent de;
    if (dir)
      de = dir->lookup(fname);
    if (!de) {
      std::cerr << argv[i] << ": no such file or directory" << std::endl;
      continue;
    }
    Ref<Inode> ip = fs().iget(de.inum());
    dir->mtouch();
    de.set_inum(0);
    if (ip->i_nlink > 1) {
      --ip->i_nlink;
      ip->mtouch();
    } else {
      ip->clear();
      fs().ifree(ip->inum());
    }
  }
}

void cmd_dump(int argc, char **argv) {
  unique_fd fd(open(fs_path(), O_RDONLY));
  if (fd == -1) {
    perror(fs_path());
    exit(1);
  }

  filsys s;
  if (!pread(fd, &s, sizeof(s), SECTOR_SIZE * SUPERBLOCK_SECTOR)) {
    perror("pread");
    exit(1);
  }

  printf("* superblock contents:\n");
#define DUMP(field) printf("%11s: %d\n", #field, s.field)
  DUMP(s_isize);
  DUMP(s_fsize);
  DUMP(s_nfree);
  printf("%11s:", "s_free");
  for (int i = 0; i < std::min<int>(s.s_nfree, array_size(s.s_free)); i++) {
    if (i && !(i % 10))
      printf("\n           ");
    printf(" %5d", s.s_free[i]);
  }
  printf("\n");
  DUMP(s_ninode);
  printf("%11s:", "s_inode");
  for (int i = 0; i < std::min<int>(array_size(s.s_inode), s.s_ninode); i++) {
    if (i && !(i % 10))
      printf("\n           ");
    printf(" %5d", s.s_inode[i]);
  }
  printf("\n");
  DUMP(s_flock);
  DUMP(s_ilock);
  DUMP(s_fmod);
  DUMP(s_ronly);
  printf("%11s: %s\n", "s_time",
         fmttime(s.s_time[0] << 16 | s.s_time[1]).c_str());
  DUMP(s_uselog);
  DUMP(s_dirty);
#undef DUMP
  if (!s.s_uselog)
    return;

  loghdr h;
  try {
    read_loghdr(fd, &h, s.s_fsize);
  } catch (std::exception &e) {
    return;
  }
  printf("\n* loghdr contents:\n");
#define DUMP(spec, field) printf("%11s: " #spec "\n", #field, h.field)
  DUMP(0x % x, l_magic);
  DUMP(% d, l_hdrblock);
  DUMP(% d, l_logsize);
  DUMP(% d, l_mapsize);
  DUMP(% d, l_checkpoint);
  DUMP(% u, l_sequence);
#undef DUMP
}

void cmd_usedblocks(int argc, char **argv) {
  const filsys &sb = fs(V6FS::V6_RDONLY).superblock();
  int nblocks = sb.s_fsize - sb.datastart();
  int nfree = fs_num_free_blocks(fs());
  printf("%d used blocks (out of %d)\n", (nblocks - nfree), nblocks);
  Bitmap bm = fs_freemap(fs());
  int c = 0;
  for (unsigned i = bm.min_index(); i < bm.max_index(); ++i)
    if (!bm.at(i)) {
      if (!(c % 10))
        printf(c ? "\n    " : "    ");
      printf(" %5d", i);
      ++c;
    }
  if (c)
    printf("\n");
  if (nfree != bm.num1()) {
    printf("nfree = %d, bn.num1() = %d\n", nfree, bm.num1());
    assert(nfree == bm.num1());
  }
}

void cmd_usedinodes(int argc, char **argv) {
  const filsys &sb = fs(V6FS::V6_RDONLY).superblock();
  unsigned ninodes = sb.s_isize * INODES_PER_BLOCK;
  int nfree = fs_num_free_inodes(fs());
  printf("%d used inodes (out of %u)\n", (ninodes - nfree), ninodes);
  Bitmap bm = fs_freemap(fs());
  int c = 0;
  for (unsigned i = ROOT_INUMBER; i <= ninodes; ++i)
    if (fs().iget(i)->i_mode & IALLOC) {
      if (!(c % 10))
        printf(c ? "\n    " : "    ");
      printf(" %5d", i);
      ++c;
    }
  if (c)
    printf("\n");
}

// Fill all free blocks with garbage.
void cmd_deface(int argc, char **argv) {
  std::string garbage;
  while (garbage.size() < SECTOR_SIZE)
    garbage += "This is garbage. ";
  garbage.resize(SECTOR_SIZE);

  Bitmap bm = fs_freemap(fs());
  for (unsigned i = bm.min_index(); i < bm.max_index(); ++i)
    if (bm.at(i)) {
      Ref<Buffer> bp = fs().bget(i);
      memcpy(bp->mem_, garbage.data(), sizeof(bp->mem_));
      bp->bdwrite();
    }
}

std::map<std::string, std::function<void(int, char **)>> commands{
    {"block", cmd_block},
    {"iblock", cmd_iblock},
    {"ls", cmd_ls},
    {"cat", cmd_cat},
    {"put", cmd_put},
    {"stat", cmd_stat},
    {"truncate", cmd_truncate},
    {"unlink", cmd_unlink},
    {"dump", cmd_dump},
    {"usedblocks", cmd_usedblocks},
    {"usedinodes", cmd_usedinodes},
    {"deface", cmd_deface},
};

[[noreturn]] void usage(int err = 1) {
  auto &out = err ? std::cerr : std::cout;
  out << "usage:\n";
  for (auto [name, fn] : commands)
    out << "  " << progname << " " << name << " [args...]\n";
  exit(err);
}

int main(int argc, char **argv) {
  if (argc == 0)
    progname = "v6";
  else if ((progname = strrchr(argv[0], '/')))
    ++progname;
  else
    progname = argv[0];

  if (argc < 2)
    usage();
  auto cmd = commands.find(argv[1]);
  if (cmd == commands.end())
    usage();
  cmd->second(argc - 2, argv + 2);
}
