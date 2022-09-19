#pragma once

#include <variant>

#include "v6fs.hh"

// A BlockPath is a compact representation of a vector of 0-3 9-bit
// values.  It is used to represent indexes in block pointer arrays.
// The first (highest) index is an index for the i_addr field of an
// inode.  Subsequent values (if present) are indexes into indirect
// blocks.  We have nine bits instead of eight to enable sentinel
// values one beyond the end of a block.
//
// A BlockPath can implicitly be converted to the highest index.  The
// tail() method returns a new BlockPath in which the highest level is
// the second highest level of the previous BlockPath.  When called on
// a BlockPath of height 1, tail() returns an invalid blockpath of
// height 0.
struct BlockPath {
  uint32_t val_;

  constexpr explicit BlockPath(uint32_t val) : val_(val) {}
  constexpr operator uint16_t() const { return val_ >> 23; }
  explicit operator bool() = delete;
  constexpr uint8_t height() const { return val_ & 3; }
  // True if path starts at inode rather than indirect block
  constexpr bool from_inode() const { return val_ & 4; }

  [[nodiscard]] BlockPath tail() const {
    if (!height())
      throw std::logic_error("BlockPath::tail: empty index list");
    return BlockPath((val_ & ~7) << 9 | (height() - 1));
  }

  // If a BlockPath pth is being used as a sentinel block,
  // pth.tail_at(i) represents the sentinel value in the child
  // pointer i.  If i is before pth, then tail_at returns a path
  // greater than that of all children, if i is after pth, then
  // tail_at returns a path before.  Otherwise, returns the path to
  // the sentinel block.
  [[nodiscard]] BlockPath tail_at(uint16_t i) const {
    if (!height())
      throw std::logic_error("BlockPath::tail_at: empty index list");
    if (i == *this)
      return tail();
    int h = height() - 1;
    // Special case for asymmetry of ILARG inodes
    if (from_inode() && h > 0)
      h = i < IADDR_SIZE - 1 ? 1 : 2;
    if (i < *this)
      return BlockPath(0x80400000 << 9 * (2 - h) | h);
    return BlockPath(h);
  }

  // true if the height() most significant values are all 0.
  constexpr bool is_zero() const {
    return val_ >> (5 + 9 * (3 - height())) == 0;
  }

  static BlockPath make(uint16_t b1) { return BlockPath(b1 << 23 | 5); }
  static BlockPath make(uint16_t b1, uint16_t b2) {
    return BlockPath(b1 << 23 | b2 << 14 | 6);
  }
  static BlockPath make(uint16_t b1, uint16_t b2, uint16_t b3) {
    return BlockPath(b1 << 23 | b2 << 14 | b3 << 5 | 7);
  }
};

// Compute the path to reach a particular block number in a file.  The
// i_mode field is required to check for the ILARG flag, which
// indicates the use of indirect blocks.  The maximum block number in
// a file is 0xffff, but the blockno is a uint32_t so that 0x10000 can
// be used to signal one beyond the last block of a file.
BlockPath blockno_path(uint16_t mode, uint32_t blockno);

// Return the first block path beyond the end of a size-byte file.
BlockPath sentinel_path(uint16_t mode, uint32_t size);

// Convert a BlockPath rooted in an inode to the block number of the
// underlying file.  Infers whether the inode is ILARG or not from the
// height of the path.
uint16_t blockpath_no(BlockPath pth);

// Data structure for an array of block pointers, which can be either
// an inode, an indirect block, or a double-indirect block.  This
// abstracts away the difference betwen inodes and indirect blocks
// (which are different sizes and must be marked dirty in different
// ways), but maintains a reference to the underlying
// buffer or inode to prevent it from being evicted.
struct BlockPtrArray {
  std::variant<Ref<Inode>, Ref<Buffer>> ref;

  BlockPtrArray(Ref<Inode> ip) : ref(std::move(ip)) {}
  BlockPtrArray(Ref<Buffer> bp) : ref(std::move(bp)) {}

  bool is_inode() const { return ref.index() == 0; }

  uint16_t size() const {
    static constexpr uint16_t sizes[] = {IADDR_SIZE, INDBLK_SIZE};
    return sizes[ref.index()];
  }

  uint16_t at(unsigned idx) {
    if (idx >= size())
      throw std::out_of_range("BlockPtrArray size exceeded");
    return data()[idx];
  }
  void set_at(unsigned idx, uint16_t blkno) {
    if (idx >= size())
      throw std::out_of_range("BlockPtrArray size exceeded");
    fs().patch(data()[idx], blkno);
  }

  // Returns the location of the pointer in the disk image
  uint32_t pointer_offset(unsigned idx) {
    return fs().disk_offset(data() + idx);
  }

  Ref<Buffer> fetch_at(unsigned idx) {
    uint16_t bn = at(idx);
    return bn ? fs().bread(bn) : nullptr;
  }

  V6FS &fs() {
    return visit([](auto &r) -> V6FS & { return r->fs(); }, ref);
  }

  // Check all pointers, return false if any of them appear
  // corrupted (as might happen if an indirect block is not
  // propertly initialized).
  bool check(bool dbl_indir = false);

private:
  uint16_t *data() {
    static constexpr struct {
      uint16_t *operator()(Ref<Inode> &ip) const { return ip->i_addr; }
      uint16_t *operator()(Ref<Buffer> &bp) const {
        return reinterpret_cast<uint16_t *>(bp->mem_);
      }
    } op;
    return visit(op, ref);
  }
};
