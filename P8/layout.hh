/*
 * This header contains the data structures describing bytes
 * physically on disk in the V6 file system.  These assume a
 * little-endian byte order.
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <type_traits>

using std::size_t;
using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

constexpr size_t SECTOR_SIZE = 512;
constexpr uint16_t BOOTBLOCK_SECTOR = 0;
constexpr uint16_t SUPERBLOCK_SECTOR = 1;
constexpr uint16_t INODE_START_SECTOR = 2;
constexpr uint16_t ROOT_INUMBER = 1;
constexpr uint16_t BOOTBLOCK_MAGIC_NUM = 0407;
constexpr uint32_t MAX_FILE_SIZE = 0xffffff;

struct filsys {
  uint16_t s_isize;      // size in blocks of I list
  uint16_t s_fsize;      // size in blocks of entire volume (w/o log)
  uint16_t s_nfree;      // number of in core free blocks (0-100)
  uint16_t s_free[100];  // in core free blocks
  uint16_t s_ninode;     // number of in core I nodes (0-100)
  uint16_t s_inode[100]; // in core free I nodes
  uint8_t s_flock;       // lock during free list manipulation
  uint8_t s_ilock;       // lock during I list manipulation
  uint8_t s_fmod;        // super block modified flag
  uint8_t s_ronly;       // mounted read-only flag
  uint16_t s_time[2];    // current date of last update

  // Modern additions for CS111
  uint8_t s_uselog; // Use log
  uint8_t s_dirty;  // File system was not cleanly shut down

  uint16_t pad[47]; // aligns struct filsys to be 512 bytes in
                    // size (the block size!)

  // First data block of file
  uint16_t datastart() const { return INODE_START_SECTOR + s_isize; }
};
static_assert(std::is_standard_layout_v<filsys>,
              "on-disk data strcutures must have standard layout");
static_assert(sizeof(filsys) == SECTOR_SIZE,
              "superblock must be exactly one sector");

// Number of block numbers in an inode
constexpr size_t IADDR_SIZE = 8;
// Number of block numbers in an indirect or double-indirect block
constexpr size_t INDBLK_SIZE = SECTOR_SIZE / sizeof(uint16_t);

/**
 * The inode is the focus of all file activity in unix.  There is a unique
 * inode allocated for each active file and directory. The format of an
 * inode is the same whether the inode is on disk or in memory.
 */
struct inode {
  uint16_t i_mode;             // bit vector of file type and permissions
  uint8_t i_nlink;             // number of references to file
  uint8_t i_uid;               // owner
  uint8_t i_gid;               // group of owner
  uint8_t i_size0;             // most significant byte of size
  uint16_t i_size1;            // lower two bytes of size (size is encoded
                               // in a three-byte number)
  uint16_t i_addr[IADDR_SIZE]; // block numbers for the file's
                               // data. For small files, these are
                               // the blocks of data. For large
                               // files these are indirect blocks,
                               // except for i_addr[7], which is a
                               // doubly indirect block. A block
                               // number of zero means "no such
                               // block".
  uint32_t i_atime;            // access time
  uint32_t i_mtime;            // modify time

  uint8_t &minor() { return *reinterpret_cast<uint8_t *>(i_addr); }
  uint8_t &major() { return (&minor())[1]; }

  uint32_t mtime() const { return i_mtime << 16 | i_mtime >> 16; }
  void mtime(uint32_t t) { i_mtime = t << 16 | t >> 16; }
  uint32_t atime() const { return i_atime << 16 | i_atime >> 16; }
  void atime(uint32_t t) { i_atime = t << 16 | t >> 16; }

  uint32_t size() const { return i_size0 << 16 | i_size1; }
  void size(uint32_t sz) {
    i_size0 = sz >> 16;
    i_size1 = sz & 0xffff;
  };
};
static_assert(std::is_standard_layout_v<inode>,
              "on-disk data strcutures must have standard layout");

constexpr uint16_t INODES_PER_BLOCK = SECTOR_SIZE / sizeof(inode);

/* modes */
constexpr uint16_t IALLOC = 0100000; // file is used
constexpr uint16_t IFMT = 060000;    // mask for type of file
constexpr uint16_t IFDIR = 040000;   //  - directory
constexpr uint16_t IFCHR = 020000;   //  - character special
constexpr uint16_t IFBLK = 060000;   //  - block special
constexpr uint16_t IFREG = 000000;   //  - 0 means regular file
constexpr uint16_t ILARG = 010000;   // large addressing algorithm
constexpr uint16_t ISUID = 04000;    // set user id on execution
constexpr uint16_t ISGID = 02000;    // set group id on execution
constexpr uint16_t ISVTX = 01000;    // save swapped text even after use
constexpr uint16_t IREAD = 0400;     // read, write, execute permissions
constexpr uint16_t IWRITE = 0200;
constexpr uint16_t IEXEC = 0100;

/**
 * The Unix Version 6 code didn't use a structure like this, but this
 * matches the format of a directory entry.
 */
struct direntv6 {
  uint16_t d_inumber;
  char d_name[14];

  std::string_view name() const {
    const char *e = std::find(d_name, d_name + sizeof(d_name), '\0');
    return std::string_view{d_name, std::size_t(e - d_name)};
  }
  void name(std::string_view sv) {
    if (sv.size() > sizeof(d_name))
      throw std::length_error("direntv6: maximum name length exceeded");
    std::copy(sv.begin(), sv.end(), d_name);
    std::fill(d_name + sv.size(), d_name + sizeof(d_name), '\0');
  }
};
static_assert(std::is_standard_layout_v<direntv6>,
              "on-disk data strcutures must have standard layout");

template <typename T, size_t N> inline size_t array_size(const T (&)[N]) {
  return N;
}
