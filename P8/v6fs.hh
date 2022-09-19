#pragma once

#include <cassert>
#include <memory>
#include <utility>

#include "cache.hh"
#include "layout.hh"
#include "log.hh"

struct V6FS;
struct Inode;
struct Dirent;

struct Buffer : CacheEntryBase {
  alignas(uint32_t) char mem_[SECTOR_SIZE]; // Actual bytes in the buffer

  uint16_t blockno() const { return id_; }
  void bwrite();   // Write the buffer immediately
  void bdwrite() { // Write buffer later (delayed write)
    initialized_ = dirty_ = true;
  }
  void writeback() override { bwrite(); }
  template <typename T> T &at(size_t i) {
    if (i >= SECTOR_SIZE / sizeof(T))
      throw std::out_of_range("Buffer::at");
    return reinterpret_cast<T *>(mem_)[i];
  }
};

enum class DoLog : bool {
  NOLOG = false, // Don't write inode changes to journal
  LOG = true,    // Write inode changes to journal
};

// In-memory cache of an inode
struct Inode : inode, CacheEntryBase {
  uint16_t inum() const { return id_; }
  void writeback() override { put(); }
  void put();
  void truncate(uint32_t size = 0, DoLog = DoLog::LOG);
  void set_size(uint32_t s);
  void clear();

  // Read block at particular offset in file
  Ref<Buffer> getblock(uint16_t blockno, bool allocate = false);

  // Look up a filename if this inode is a directory
  Dirent lookup(std::string_view name);

  // Look up a filename if this inode is a directory, and if the
  // filename doesn't exist create a directory entry for it.
  Dirent create(std::string_view name);

  void atouch();                   // Update atime
  void mtouch(DoLog = DoLog::LOG); // Update mtime
  inode &raw() { return *this; }   // On-disk format

private:
  bool make_large();
  void make_small(DoLog = DoLog::LOG);
};

struct Dirent {
  Ref<Inode> dir_;
  Ref<Buffer> bp_;
  direntv6 *de_ = nullptr;

  Dirent() = default;
  Dirent(Ref<Inode> dir, Ref<Buffer> bp, direntv6 *de)
      : dir_(dir), bp_(std::move(bp)), de_(de) {}
  explicit operator bool() const { return de_; }
  V6FS &fs() const { return dir_->fs(); }

  uint16_t inum() const { return de_->d_inumber; }
  void set_inum(uint16_t inum) const;
  std::string_view name() const { return de_->name(); }
  void name(std::string_view sv) const { de_->name(sv); }
};

// Cursor for reading an inode
struct Cursor {
  const Ref<Inode> ip_;

  // If bp_ is not nullptr, then it points to block number
  // (pos_-1)/SECTOR_SIZE.  This is because next() returns a pointer
  // to a data structure that ends at pos_, so we want to maintain a
  // reference to the buffer containing those bytes.
  Ref<Buffer> bp_;

  uint32_t pos_ = 0; // Current position in file

  Cursor(Ref<Inode> ip) : ip_(std::move(ip)) {}
  Cursor(Inode *ip) : ip_(ip) {}
  V6FS &fs() const { return ip_->fs(); }
  void seek(uint32_t pos);
  uint32_t tell() const { return pos_; }
  int read(void *buf, size_t n);
  int write(const void *buf, size_t n);

  template <typename T> T *next() {
    static_assert(sizeof(T) <= SECTOR_SIZE && SECTOR_SIZE % sizeof(T) == 0);
    return static_cast<T *>(readref(sizeof(T)));
  }
  template <typename T> T *writenext() {
    static_assert(sizeof(T) <= SECTOR_SIZE && SECTOR_SIZE % sizeof(T) == 0);
    return static_cast<T *>(writeref(sizeof(T)));
  }

private:
  // Return pointer to the next n bytes (which must fit within an
  // aligned sector), or nullptr at EOF.  Skips empty blocks in
  // sparse files.
  void *readref(size_t n);
  // Like readref, but allocates a block to fill file holes and/or
  // extends the length of the file if necessary.
  void *writeref(size_t n);
};

struct FScache {
  Cache<Buffer> b;
  Cache<Inode> i;
  explicit FScache(size_t bsize = 16, size_t isize = 100)
      : b(bsize), i(isize) {}
};

struct V6FS {
  const bool readonly_;
  bool unclean_;
  const unique_fd fd_;
  FScache &cache_;
  std::unique_ptr<V6Log> log_;
  filsys superblock_;

  static constexpr unsigned V6_RDONLY = 0x1;
  static constexpr unsigned V6_MUST_BE_CLEAN = 0x2;
  static constexpr unsigned V6_NOLOG = 0x4;
  static constexpr unsigned V6_MKLOG = 0x8;
  static constexpr unsigned V6_REPLAY = 0x10;
  V6FS(std::string path, FScache &cache, int flags = 0);
  V6FS(const V6FS &) = delete;
  ~V6FS();

  bool sync();       // Write all dirty buffers.
  void invalidate(); // Invalidate all buffers and re-read superblock.

  Ref<Buffer> bread(uint16_t blockno); // Read block from disk

  // Get buffer for block without reading it (when you are about to
  // overwrite the block anyway and don't need the old contents).
  Ref<Buffer> bget(uint16_t blockno) { return cache_.b(this, blockno); }

  Ref<Inode> iget(uint16_t inum); // Get inode by number

  filsys &superblock() { return superblock_; }
  const filsys &superblock() const { return superblock_; }

  uint16_t iblock(uint16_t inum); // Return block number containing inum
  // index of inum without block iblock(inum)
  static uint16_t iindex(uint16_t inum) {
    return (inum - ROOT_INUMBER) % INODES_PER_BLOCK;
  }
  Ref<Inode> namei(std::string path, uint16_t start = ROOT_INUMBER);

  bool badblock(uint16_t blockno) const {
    return blockno < superblock().datastart() ||
           blockno >= superblock().s_fsize;
  }

  Tx begin() const { return log_ ? log_->begin() : Tx(); }

  // Allocate a block, fill it with zeros.  Metadata is true for
  // indirect blocks and directory blocks, and false for regular
  // file data blocks.  (When metadata is false, a block should not
  // be re-zeroed on playing back the log.)
  Ref<Buffer> balloc(bool metadata);

  // Free a block
  void bfree(uint16_t blockno);

  Ref<Inode> ialloc();
  void ifree(uint16_t inum);

  void readblock(void *mem, uint32_t blockno);
  void writeblock(const void *mem, uint32_t blockno);

  struct CacheInfo {
    uint32_t offset;       // Location on disk of bytes
    CacheEntryBase *entry; // Current cache entry of bytes
  };
  // Return information about a pointer to a cached data structure.
  // If n > 1, throws an error if p+n is outside the cache entry.
  CacheInfo cache_info(void *p, size_t n = 0);

  // Takes a pointer to something in the cache and returns the
  // offset of the corresponding bytes on disk.
  uint32_t disk_offset(void *p) { return cache_info(p).offset; }

  // Patches a data item and marks the underlying cache object
  // dirty.  The remove_reference is just to make place v in a
  // non-deduced context
  template <typename T> void patch(T &t, std::remove_reference_t<T> v) {
    patch(t = v);
  }
  template <typename T> void patch(T &t) { log_patch(&t, sizeof(t)); }
  void log_patch(void *bytes, size_t len);

private:
  // Block allocation using the original V6 free list mechanism
  uint16_t balloc_freelist();
  void bfree_freelist(uint16_t blockno);
};

// Returns true after it is called $CRASH_AT times (or never if that
// environment variable is not set).
bool should_crash();
// Crashes the program.
void crash();
