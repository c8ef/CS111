#pragma once

#include <cstring>
#include <memory>
#include <stdexcept>

// Simple bitmap type similar to std::vector<bool>, but with a few
// extra features helpful in a file system context.  Specifically:
//
//   - You can save a bitmap to disk and load it (on a machine with
//     the same byte order), by reading or writing datasize() bytes
//     to/from memory at data().  The datasize might be slightly
//     larger than the number of bytes required, and any non-zero bits
//     out of range will mess up some of the algorithms.  You can zero
//     out the extra bits with tidy() to avoid this problem.
//
//   - You can find the next set 1 bit from an arbitrary starting
//     location using find1().  This is useful if 1 bits represent
//     free blocks and you want to find a free block.
//
//   - You can count the number of 1 bits with num1().  This is an
//     O(n) operation, but not that bad since it's still pretty fast.
//     Used to compute free space.
//
//   - There's an optional "min_index" argument so the bitmap can
//     represent a non-zero-based range of indices.
struct Bitmap {
  using chunk_type = std::uint32_t;

  // The valid index range is [min_index, max_index)
  explicit Bitmap(std::size_t max_index = 0, std::size_t min_index = 0)
      : nbits_(max_index - min_index),
        nchunks_(chunkno(nbits_ + bits_per_chunk - 1)),
        mem_(new chunk_type[nchunks_]), zero_(min_index) {
    std::memset(mem_.get(), 0, datasize());
  }
  Bitmap(Bitmap &&) = default;
  Bitmap &operator=(Bitmap &&) = default;

  // Valid range of indices
  std::size_t min_index() const { return zero_; }
  std::size_t max_index() const { return zero_ + nbits_; }

  // A "simulated" reference to a single bit, to which you can
  // assign true or false.  Needed since one byte (char&) is the
  // smallest actual C++ reference.
  class bitref {
    chunk_type &chunk_;
    const chunk_type bit_;
    bitref(chunk_type &chunk, chunk_type bit) : chunk_(chunk), bit_(bit) {}
    friend Bitmap;

  public:
    bitref &operator=(const bitref &) = delete;
    operator bool() const { return chunk_ & bit_; }
    const bitref &operator=(bool v) const {
      if (v)
        chunk_ |= bit_;
      else
        chunk_ &= ~bit_;
      return *this;
    }
  };

  // Access one bit of bitmap
  bool at(std::size_t n) const {
    check(n -= zero_);
    return mem_[chunkno(n)] & chunkbit(n);
  }
  bitref at(std::size_t n) {
    check(n -= zero_);
    return {mem_[chunkno(n)], chunkbit(n)};
  }

  friend bool operator==(const Bitmap &a, const Bitmap &b) {
    return a.min_index() == b.min_index() && a.max_index() == b.max_index() &&
           !std::memcmp(a.data(), b.data(), a.datasize());
  }
  friend bool operator!=(const Bitmap &a, const Bitmap &b) { return !(a == b); }

  // Return the first 1 bit, starting at position start.  Returns -1
  // if there are no bits set in the entire Bitmap.
  int find1(std::size_t start = 0) const;

  // Compute the number of 1s in the map
  int num1() const;

  // The Bitmap can be saved and restored by copying datasize()
  // bytes of data from or to data().
  void *data() { return mem_.get(); }
  const void *data() const { return mem_.get(); }
  std::size_t datasize() const { return nchunks_ * sizeof(chunk_type); }

  // Makes sure there aren't weird extraneous 1 bits above the
  // maximum valid bit number.  Such vits could mess up the find1()
  // and num1() functions.
  void tidy();

private:
  std::size_t nbits_;
  std::size_t nchunks_;
  std::unique_ptr<chunk_type[]> mem_;
  std::size_t zero_;

  static constexpr std::size_t bits_per_chunk = 8 * sizeof(chunk_type);
  static const std::int8_t bytelsb[256];
  static const std::uint8_t bytepop[256];

  // Return the index of the lowest set bit in v, or -1 if v is 0.
  static int lsb(chunk_type v) {
    return v ? lsb_helper<8 * sizeof(chunk_type)>(v) : -1;
  }

  template <size_t N> static inline int lsb_helper(chunk_type v) {
    if constexpr (N <= 8)
      return bytelsb[v];
    else {
      constexpr chunk_type bit = 1 << N / 2;
      if (chunk_type low = v & (bit - 1))
        return lsb_helper<N / 2>(low);
      return N / 2 + lsb_helper<N / 2>(v >> N / 2);
    }
  }

  static constexpr std::size_t chunkno(std::size_t bitno) {
    return bitno / bits_per_chunk;
  }
  static constexpr chunk_type chunkbit(std::size_t bitno) {
    return chunk_type(1) << bitno % bits_per_chunk;
  }
  void check(std::size_t n) const {
    if (n >= nbits_)
      throw std::out_of_range("Bitmap: index out of range");
  }
};
