#include <cstring>
#include <iostream>
#include <map>
#include <set>

#include <unistd.h>

#include "bitmap.hh"
#include "blockpath.hh"
#include "fsops.hh"

const char *progname;
FScache cache(30);

struct Fsck {
  V6FS &fs_;
  Bitmap freemap_;
  std::vector<uint8_t> nlinks_;
  bool corrupted_ = false;
  std::ostream &out_ = std::cout;
  std::string ctx_;

  std::map<uint32_t, std::vector<uint8_t>> patches_;
  // Keep track of link additions separately from other patches, as
  // they could require block allocation, so we want to do them
  // after all other fixes.
  struct newlink {
    uint16_t dirino;
    uint16_t ino;
    std::string name;
    newlink(uint16_t di, uint16_t i, std::string n)
        : dirino(di), ino(i), name(std::move(n)) {}
  };
  std::vector<newlink> newlinks_;

  struct saved_context {
    Fsck *target = nullptr;
    std::string oldcontext;
    saved_context() {}
    saved_context(Fsck *t, std::string c) : target(t), oldcontext(t->ctx_) {
      t->ctx_ = std::move(c);
    }
    saved_context(const saved_context &) = delete;
    saved_context(saved_context &&c)
        : target(c.target), oldcontext(std::move(c.oldcontext)) {
      c.target = nullptr;
    }
    saved_context &operator=(saved_context &&c) {
      target = c.target;
      oldcontext = std::move(c.oldcontext);
      c.target = nullptr;
      return *this;
    }
    ~saved_context() {
      if (target)
        target->ctx_ = oldcontext;
    }
  };

  Fsck(V6FS &fs);

  [[nodiscard]] saved_context context(std::string ctx) { return {this, ctx}; }
  std::ostream &out() const {
    if (ctx_.empty())
      return out_;
    return out_ << ctx_ << ": ";
  }

  bool scan_blocks(Ref<Inode> ip) {
    if (uint16_t type = ip->i_mode & IFMT; type == IFCHR || type == IFBLK)
      return true;
    return scan_blocks(ip, sentinel_path(ip->i_mode, ip->size()));
  }
  bool scan_blocks(BlockPtrArray ba, BlockPath end);

  bool scan_inodes();

  bool scan_directory(Ref<Inode> ip, uint16_t parent = ROOT_INUMBER);

  bool fix_nlink();
  void rebuild_freelist();

  bool valid_inum(uint16_t inum) const {
    return inum >= ROOT_INUMBER && inum < nlinks_.size();
  }

  // Note std::remove_reference_t is to place src in a "non-deduced
  // context" so, e.g., patch(&x, 0) works when x isn't an int.
  template <typename T> void patch(T *dst, std::remove_reference_t<T> src) {
    patch(fs_.disk_offset(dst), &src, sizeof(*dst));
  }
  void patch(uint32_t offset, const void *src, size_t n) {
    const uint8_t *p = static_cast<const uint8_t *>(src);
    patches_.emplace(offset, std::vector(p, p + n));
  }

  void patch16(uint32_t offset, uint16_t src) {
    patch(offset, &src, sizeof(src));
  }

  void apply();
};

Fsck::Fsck(V6FS &fs)
    : fs_(fs), freemap_(fs_.superblock().s_fsize, fs_.superblock().datastart()),
      nlinks_(ROOT_INUMBER + fs_.superblock().s_isize * INODES_PER_BLOCK, 0) {
  memset(freemap_.data(), 0xff, freemap_.datasize());
  freemap_.tidy();
}

bool Fsck::scan_blocks(BlockPtrArray ba, BlockPath end) {
  if (!ba.is_inode() && !ba.check(end.height() == 2))
    // Zero out indirect block pointer in parent
    return false;

  bool res = true;
  for (unsigned i = 0, e = ba.size(); i < e; ++i)
    if (uint16_t bn = ba.at(i)) {
      if (fs_.badblock(bn))
        out() << "block " << bn << ": bad block number in inode\n";
      else if (i > end || (i == end && end.tail().is_zero()))
        out() << "block " << bn << ": allocated beyond end of file\n";
      else if (!freemap_.at(bn))
        out() << "block " << bn << ": cross-allocated\n";
      else {
        freemap_.at(bn) = false;
        if (end.height() <= 1 || scan_blocks(ba.fetch_at(i), end.tail_at(i)))
          continue;
      }
      patch16(ba.pointer_offset(i), 0);
      res = false;
    }
  return res;
}

bool Fsck::scan_inodes() {
  const unsigned end = nlinks_.size();
  bool res = true;
  for (unsigned ino = ROOT_INUMBER; ino < end; ++ino) {
    auto sc = context("inode " + std::to_string(ino));
    if (!scan_blocks(fs_.iget(ino)))
      res = false;
  }
  return res;
}

bool Fsck::scan_directory(Ref<Inode> ip, uint16_t parent) {
  saved_context _sc = context(ctx_ + "/");
  if (!parent)
    parent = ip->inum();
  bool res = true, dot_ok = false, dotdot_ok = false;
  std::set<std::string> names;
  for (Cursor c{ip}; direntv6 *p = c.next<direntv6>();) {
    if (!p->d_inumber)
      continue;
    std::string name(p->name());
    if (!valid_inum(p->d_inumber)) {
      out() << "invalid inumber " << p->d_inumber << " for " << name << "\n";
      res = false;
      patch(&p->d_inumber, 0);
      continue;
    }
    if (names.count(name)) {
      out() << "duplicate directory entry for \"" << p->name() << "\"\n";
      res = false;
      patch(&p->d_inumber, 0);
      continue;
    }
    names.emplace(p->name());
    if (name == ".") {
      if (p->d_inumber != ip->inum()) {
        out() << "incorrect \".\" inumber\n";
        res = false;
        patch(&p->d_inumber, ip->inum());
      }
      dot_ok = true;
      ++nlinks_.at(ip->inum());
      continue;
    }
    if (name == "..") {
      if (p->d_inumber != parent) {
        out() << "incorrect \"..\" inumber\n";
        res = false;
        patch(&p->d_inumber, parent);
      }
      dotdot_ok = true;
      ++nlinks_.at(parent);
      continue;
    }
    ++nlinks_.at(p->d_inumber);
    Ref<Inode> eip = fs_.iget(p->d_inumber);
    if (!(eip->i_mode & IALLOC)) {
      out() << "directory entry " << name << " for unallocated inode "
            << p->d_inumber << "\n";
      res = false;
      --nlinks_.at(p->d_inumber);
      patch(&p->d_inumber, 0);
      continue;
    }
    if ((eip->i_mode & IFMT) == IFDIR) {
      if (nlinks_.at(p->d_inumber) != 1) {
        out() << "hard link \"" << name << "\" to directory " << p->d_inumber
              << "\n";
        res = false;
        --nlinks_.at(p->d_inumber);
        patch(&p->d_inumber, 0);
        continue;
      }
      saved_context _sc2 = context(ctx_ + name);
      if (!scan_directory(eip, ip->inum()))
        res = false;
    }
  }
  if (!dot_ok) {
    out() << "missing \".\"\n";
    newlinks_.emplace_back(ip->inum(), ip->inum(), ".");
    ++nlinks_.at(ip->inum());
  }
  if (!dotdot_ok) {
    out() << "missing \"..\"\n";
    newlinks_.emplace_back(ip->inum(), parent, "..");
    ++nlinks_.at(parent);
  }
  return res && dot_ok && dotdot_ok;
}

void Fsck::apply() {
  fs_.invalidate();
  for (const auto &[pos, contents] : patches_) {
    assert(pos % SECTOR_SIZE + contents.size() <= SECTOR_SIZE);
    Ref<Buffer> bp = fs_.bread(pos / SECTOR_SIZE);
    memcpy(bp->mem_ + pos % SECTOR_SIZE, contents.data(), contents.size());
    bp->bdwrite();
  }
  patches_.clear();
  fs_.sync();

  fs_.superblock().s_uselog = false; // No log support yet
  rebuild_freelist();

  for (const auto &[dino, ino, name] : newlinks_) {
    Ref<Inode> ip = fs_.iget(dino);
    Dirent de = ip->create(name);
    de.set_inum(ino);
  }
  newlinks_.clear();
  fs_.sync();
}

void Fsck::rebuild_freelist() {
  fs_.superblock().s_nfree = 0;
  const uint16_t start = INODE_START_SECTOR + fs_.superblock().s_isize;
  // Since freelist is FIFO, going backwards may lead to more
  // contiguous allocation.
  for (uint16_t bn = fs_.superblock().s_fsize; bn-- > start;)
    if (freemap_.at(bn))
      fs_.bfree(bn);
}

bool Fsck::fix_nlink() {
  // Doesn't handle case of > 255 links
  bool res = true;
  const uint32_t stop = nlinks_.size();
  const inode zero{};
  for (uint32_t i = ROOT_INUMBER; i < stop; ++i) {
    Ref<Inode> ip = fs_.iget(i);
    int n = nlinks_.at(i);
    if (n == 0) {
      if (ip->i_mode & IALLOC) {
        out() << "clearing unreachable inode " << i << "\n";
        res = false;
        patch<inode>(ip.get(), zero);
      }
    } else if (n != ip->i_nlink) {
      out() << "inode " << ip->inum() << ": link count " << int(ip->i_nlink)
            << " should be " << n << "\n";
      res = false;
      patch(&ip->i_nlink, n);
    }
  }
  return res;
}

int fsck(V6FS &fs, bool write = true) {
  Fsck fsck(fs);
  bool res = true;
  if (!fsck.scan_inodes()) {
    std::cout << "scan inodes required fixes\n";
    res = false;
    if (write)
      fsck.apply();
  }
  {
    bool ok = false;
    try {
      ok = fsck.freemap_ == fs_freemap(fs);
    } catch (std::exception &) {
    }
    if (!ok) {
      std::cout << "free list was incorrect\n";
      res = false;
    }
  }
  if (!fsck.scan_directory(fs.iget(ROOT_INUMBER))) {
    std::cout << "scan directories required fixes\n";
    res = false;
    if (write)
      fsck.apply();
  }
  if (!fsck.fix_nlink()) {
    std::cout << "fix link count required fixes\n";
    res = false;
  }
  if (fs.superblock().s_ninode > array_size(fs.superblock().s_inode)) {
    std::cout << "invalid s_ninode\n";
    fs.superblock().s_ninode = 0;
    res = false;
  } else
    for (uint16_t *inp = fs.superblock().s_inode,
                  *end = inp + fs.superblock().s_ninode;
         inp < end; ++inp) {
      if (*inp < ROOT_INUMBER || *inp >= fsck.nlinks_.size() ||
          fsck.nlinks_.at(*inp)) {
        std::cout << "invalid inode " << *inp << " in free list\n";
        fs.superblock().s_ninode = 0;
        res = false;
      }
    }
  if (write) {
    fsck.apply();
    // force re-scanning for free inodes
    fs.superblock().s_ninode = 0;
    fs.superblock().s_fmod = 1;
    fs.superblock().s_dirty = 0;
    fs.unclean_ = false;
  } else {
    fs.superblock().s_fmod = 0;
    fs.invalidate();
  }
  if (!res) {
    std::cout << "File system was corrupt\n";
    return 1;
  }
  return 0;
}

[[noreturn]] void usage(int exitval = 2) {
  std::cerr << "usage: " << progname << " [-y] fs-image" << std::endl;
  exit(exitval);
}

int main(int argc, char **argv) {
  if (argc == 0)
    progname = "v6";
  else if ((progname = std::strrchr(argv[0], '/')))
    ++progname;
  else
    progname = argv[0];

  bool opt_yes = false;
  int opt;
  int flags = V6FS::V6_NOLOG;
  while ((opt = getopt(argc, argv, "y")) != -1)
    switch (opt) {
    case 'y':
      opt_yes = true;
      break;
    default:
      usage();
    }

  if (optind + 1 != argc)
    usage();

  if (!opt_yes)
    flags |= V6FS::V6_RDONLY;
  int res = [&]() {
    V6FS fs(argv[optind], cache, flags);
    return fsck(fs, opt_yes);
  }();
  exit(res);
}
