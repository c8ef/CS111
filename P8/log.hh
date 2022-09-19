#pragma once

#include <cassert>
#include <cstdint>
#include <functional>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include "bitmap.hh"
#include "bufio.hh"
#include "layout.hh"
#include "logentry.hh"
#include "util.hh"

struct V6FS;

uint32_t rnd_uint32();

void read_loghdr(int fd, loghdr *hdr, uint32_t blockno);

class Tx;

struct V6Log {
  V6FS &fs_;
  FdWriter w_;
  bool in_tx_ = false;
  lsn_t sequence_;  // LSN of last written log record
  lsn_t committed_; // Highest LSN written to log
  lsn_t applied_;   // Highest LSN applied to file system
  time_t checkpoint_time_ = 0;
  loghdr hdr_;
  Bitmap freemap_;

  // True if LSN a is earlier or the same as LSN b, taking into
  // account the fact that LSNs can wrap, but the LSN space is much
  // larger than the log.
  static bool le(lsn_t a, lsn_t b) {
    constexpr lsn_t half_range = lsn_t(-1) >> 1;
    return b - a <= half_range;
  }

  V6Log(V6FS &fs);

  friend Tx;
  [[nodiscard]] Tx begin();
  void log(LogEntry::entry_type e);

  uint16_t balloc_near(uint16_t near, bool metadata);
  uint16_t balloc(bool metadata) {
    return last_balloc_ =
               balloc_near(suppress_commit_ ? 0 : last_balloc_, metadata);
  }
  void bfree(uint16_t blockno);

  void flush();      // Flush log to increase committed_
  void checkpoint(); // Write checkpoint record to increase applied_
  uint32_t space();  // Available log space

  static void create(V6FS &fs, uint16_t log_blocks = 0);

  // If true, prevents flushing the log so you eventually run out of
  // buffers.  It's just for generating test cases--leave it false.
  bool suppress_commit_ = false;

private:
  uint16_t last_balloc_ = 0; // Last allocated block
  lsn_t begin_sequence_;     // LSN of last LogBegin record
  lsn_t begin_offset;        // File offset of last LogBegin record

  // List of blocks that have been freed by previous transactions
  std::vector<uint16_t> freed_;

  void commit();
};

class Tx {
public:
  Tx() : log_(nullptr) {}
  Tx(Tx &&other) : log_(other.log_) { other.log_ = nullptr; }
  ~Tx() {
    if (log_)
      log_->commit();
  }
  // Monotonically accumulates the transaction, so you can
  // repeatedly call tx = begin();
  Tx &operator=(Tx &&other) {
    if (!log_) {
      log_ = other.log_;
      other.log_ = nullptr;
    } else // Should only have one Tx at a time
      assert(!other.log_);
    return *this;
  }

private:
  friend struct V6Log;
  V6Log *log_;
  explicit Tx(V6Log *log) : log_(log) {}
};
