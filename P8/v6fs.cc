#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "replay.hh"
#include "util.hh"
#include "v6fs.hh"

bool should_crash() {
  static int crash_at = []() {
    if (char *n = getenv("CRASH_AT"))
      return atoi(n);
    return 0;
  }();
  return crash_at > 0 && --crash_at == 0;
}

void crash() {
  fprintf(stderr, "Crashing because of CRASHAT environment variable\n");
  std::abort();
}

V6FS::V6FS(std::string path, FScache &cache, int flags)
    : readonly_(flags & V6_RDONLY),
      fd_(::open(path.c_str(), readonly_ ? O_RDONLY : O_RDWR)), cache_(cache) {
  if (fd_ == -1)
    threrror("open");

  readblock(&superblock_, SUPERBLOCK_SECTOR);
  uint16_t magic;
  if (pread(fd_, &magic, sizeof(magic), 0) != sizeof(magic))
    threrror("pread (magic)");
  if (magic != BOOTBLOCK_MAGIC_NUM)
    throw std::runtime_error("boot block missing magic number");
  unclean_ = superblock().s_dirty;

  // Legacy V6 file systems seem to have garbage at end of superblock
  if (superblock().s_uselog)
    try {
      loghdr hdr;
      read_loghdr(fd_, &hdr, superblock().s_fsize);
    } catch (std::exception &e) {
      printf("invalid log header, clearing s_uselog in superblock\n");
      superblock().s_uselog = 0;
    }

  if ((flags & V6_MUST_BE_CLEAN) && unclean_ &&
      (!superblock().s_uselog || (flags & (V6_REPLAY | V6_NOLOG)) != V6_REPLAY))
    throw std::runtime_error("file system not cleanly unmounted");
  if (!readonly_)
    superblock().s_fmod = 0;
  if (!(flags & V6_NOLOG) && !readonly_) {
    if (!superblock().s_uselog && (flags & V6_MKLOG)) {
      printf("creating journal and bitmap\n");
      V6Log::create(*this);
    }
    if (superblock().s_uselog) {
      if (unclean_) {
        V6Replay r(*this);
        r.replay();
      }
      log_ = std::make_unique<V6Log>(*this);
    }
  }
  if (!readonly_) {
    superblock().s_dirty = 1;
    writeblock(&superblock_, SUPERBLOCK_SECTOR);
  }
}

V6FS::~V6FS() {
  if (!readonly_) {
    if (log_)
      log_->checkpoint();
    else
      sync();
    log_ = nullptr;
    superblock().s_fmod = 0;
    if (!unclean_ && (!log_ || !log_->suppress_commit_))
      superblock().s_dirty = 0;
    writeblock(&superblock_, SUPERBLOCK_SECTOR);
  }
  invalidate();
}

bool V6FS::sync() {
  bool ok = true;
  if (!cache_.i.flush_dev(this))
    ok = false;
  if (!cache_.b.flush_dev(this))
    ok = false;

  // Only need to flush if we aren't logging.  Otherwise, no
  // interesting information in the superblock (because we rebuild
  // inode free list on remount, and don't use block free list).
  if (!log_ && superblock().s_fmod)
    try {
      superblock().s_fmod = 0;
      writeblock(&superblock(), SUPERBLOCK_SECTOR);
    } catch (const std::exception &e) {
      ok = false;
    }

  return ok;
}

void V6FS::invalidate() {
  cache_.i.invalidate_dev(this);
  cache_.b.invalidate_dev(this);
  readblock(&superblock(), SUPERBLOCK_SECTOR);
}

Ref<Buffer> V6FS::bread(uint16_t blockno) {
  Ref<Buffer> bp = cache_.b(this, blockno);
  if (!bp->initialized_) {
    readblock(bp->mem_, blockno);
    bp->initialized_ = true;
  }
  return bp;
}

void V6FS::readblock(void *mem, uint32_t blockno) {
  int n = pread(fd_, mem, SECTOR_SIZE, blockno * SECTOR_SIZE);
  if (n != SECTOR_SIZE) {
    if (n != -1)
      errno = EPIPE;
    threrror("pread");
  }
}

void V6FS::writeblock(const void *mem, uint32_t blockno) {
  if (should_crash())
    crash();

  if (pwrite(fd_, mem, SECTOR_SIZE, blockno * SECTOR_SIZE) != SECTOR_SIZE)
    threrror("pwrite");
}

uint16_t V6FS::iblock(uint16_t inum) {
  if (inum != 0) {
    uint16_t blockno = (inum - ROOT_INUMBER) / INODES_PER_BLOCK;
    if (blockno < superblock().s_isize && blockno < uint16_t(-2))
      return blockno + INODE_START_SECTOR;
  }
  throw std::out_of_range("iblock: invalid inum");
}

Ref<Inode> V6FS::iget(uint16_t inum) {
  Ref<Inode> ip = cache_.i(this, inum);
  if (!ip->initialized_) {
    Ref<Buffer> bp = bread(iblock(inum));
    static_cast<inode &>(*ip) = bp->at<inode>(iindex(inum));
    ip->initialized_ = true;
  }
  return ip;
}

Ref<Inode> V6FS::namei(std::string path, uint16_t start) {
  Ref<Inode> ip = iget(start);
  for (std::string_view name : path_components(path)) {
    if (!ip || !(ip->i_mode & IFDIR))
      return nullptr;
    Dirent d = ip->lookup(name);
    if (!d)
      return nullptr;
    ip = iget(d.inum());
  }
  return ip;
}

Ref<Buffer> V6FS::balloc(bool metadata) {
  if (!cache_.b.can_alloc()) {
    printf("Inode cache is full\n");
    throw resource_exhausted("block allocation out of buffers", -ENOMEM);
  }
  uint16_t bn = log_ ? log_->balloc(metadata) : balloc_freelist();
  if (!bn)
    throw resource_exhausted("no free blocks on device", -ENOSPC);
  Ref<Buffer> bp = bget(bn);
  memset(bp->mem_, 0, sizeof(bp->mem_));
  bp->bdwrite();
  return bp;
}

void V6FS::bfree(uint16_t blockno) {
  if (badblock(blockno))
    throw std::logic_error("attempt to free bad block");

  if (log_)
    log_->bfree(blockno);
  else
    bfree_freelist(blockno);
  cache_.b.free(this, blockno);
}

uint16_t V6FS::balloc_freelist() {
  // If s_free[0] == 0, we are at the end of the list and have no
  // more free blocks.
  if (superblock().s_nfree == 0 ||
      (superblock().s_nfree == 1 && superblock().s_free[0] == 0))
    return 0;

  superblock().s_fmod = 1;

  uint16_t blockno = superblock().s_free[--superblock().s_nfree];

  if (superblock().s_nfree == 0) {
    // Have to read the block we are allocating to re-fill s_free
    Ref<Buffer> bp = bread(blockno);
    memcpy(superblock().s_free, bp->mem_, sizeof(superblock().s_free));
    superblock().s_nfree = array_size(superblock().s_free);
  }

  return blockno;
}

// The free list is a linked list of blocks (terminating with the bad
// block number 0).  However, each block in the list contains pointers
// to 100 free blocks.  The first pointer points to another block in
// the free list, while the remaining 99 pointers are to free blocks
// containing garbage.
void V6FS::bfree_freelist(uint16_t blockno) {
  superblock().s_fmod = 1;

  // If our array of 100 free blocks is already full, then ship it
  // off to storage.
  if (superblock().s_nfree == array_size(superblock().s_free)) {
    Ref<Buffer> bp = cache_.b(this, blockno);
    memcpy(bp->mem_, superblock().s_free, sizeof(superblock().s_free));
    memset(bp->mem_ + sizeof(superblock().s_free), 0,
           SECTOR_SIZE - sizeof(superblock().s_free));
    superblock().s_free[0] = blockno;
    superblock().s_nfree = 1;
    bp->bwrite();
    return;
  }

  // When creating file system or when there were zero free blocks,
  // we need to place the terminating 0 in the first block pointer.
  if (superblock().s_nfree == 0) {
    superblock().s_free[0] = 0;
    superblock().s_nfree = 1;
  }

  superblock().s_free[superblock().s_nfree++] = blockno;
}

V6FS::CacheInfo V6FS::cache_info(void *_p, size_t n) {
  CacheInfo res;
  char *p = static_cast<char *>(_p);
  char *b, *e = nullptr;
  if (cache_.b.contains(p)) {
    Buffer *bp = cache_.b.entry_containing(p);
    b = bp->mem_;
    e = bp->mem_ + sizeof(bp->mem_);
    res.offset = bp->blockno() * SECTOR_SIZE + (p - b);
    res.entry = bp;
  } else if (cache_.i.contains(p)) {
    Inode *ip = cache_.i.entry_containing(p);
    inode *ii = ip;
    b = reinterpret_cast<char *>(ii);
    e = reinterpret_cast<char *>(ii + 1);
    res.offset = iblock(ip->inum()) * SECTOR_SIZE +
                 iindex(ip->inum()) * sizeof(inode) + (p - b);
    res.entry = ip;
  }
  if (!e || p < b || p >= e)
    throw std::out_of_range("cache_info: invalid pointer");
  return res;
}

Ref<Inode> V6FS::ialloc() {
  if (!cache_.i.can_alloc()) {
    printf("Inode cache is full\n");
    throw resource_exhausted("inode cache overflow", -ENOMEM);
  }
  if (superblock().s_ninode == 0) {
    // Out of free inodes?  Just scan the whole disk from the
    // start until we get 100.  This is what V6 actually did.
    unsigned end = superblock().s_isize * INODES_PER_BLOCK;
    for (unsigned i = 1;
         i <= end && superblock().s_ninode < array_size(superblock().s_inode);
         ++i) {
      Ref<Inode> ip = iget(i);
      if (!(ip->i_mode & IALLOC))
        superblock().s_inode[superblock().s_ninode++] = i;
    }
  }
  if (superblock().s_ninode == 0)
    throw resource_exhausted("out of inodes", -ENOSPC);
  Ref<Inode> ip = cache_.i(this, superblock().s_inode[--superblock().s_ninode]);
  superblock().s_fmod = 1;
  memset(&ip->raw(), 0, sizeof(inode));
  ip->initialized_ = true;
  return ip;
}

void V6FS::ifree(uint16_t inum) {
  if (inum < 1 || inum > superblock().s_isize * INODES_PER_BLOCK)
    throw std::out_of_range("ifree: invalid inum");
  if (superblock().s_ninode >= array_size(superblock().s_inode))
    return;
  superblock().s_inode[superblock().s_ninode++] = inum;
  superblock().s_fmod = 1;
}

void V6FS::log_patch(void *_p, size_t len) {
  assert(len > 0);
  uint8_t *p = reinterpret_cast<uint8_t *>(_p);
  CacheInfo ci = cache_info(p, len);
  ci.entry->mark_dirty();
  if (!log_)
    return;
  assert(log_->in_tx_);
  log_->log(LogPatch{uint16_t(ci.offset / SECTOR_SIZE),
                     uint16_t(ci.offset % SECTOR_SIZE),
                     std::vector(p, p + len)});
  ci.entry->lsn_ = log_->sequence_;
  ci.entry->logged_ = true;
}
