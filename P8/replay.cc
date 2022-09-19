#include <unistd.h>

#include <cstring>
#include <iostream>

#include "replay.hh"
#include "v6fs.hh"

V6Replay::V6Replay(V6FS &fs)
    : fs_(fs), r_(fs_.fd_),
      freemap_(fs_.superblock().s_fsize, fs_.superblock().datastart()) {
  read_loghdr(fs_.fd_, &hdr_, fs_.superblock().s_fsize);
  if (pread(fs.fd_, freemap_.data(), freemap_.datasize(),
            hdr_.mapstart() * SECTOR_SIZE) == -1)
    threrror("pread");
  freemap_.tidy();
  sequence_ = hdr_.l_sequence;
  r_.seek(hdr_.l_checkpoint);
}

void V6Replay::apply(const LogBegin &e) {}

void V6Replay::apply(const LogPatch &e) {
  auto buf = fs_.bread(e.blockno);
  // don't even try strncpy
  // i waste several hours THANKS TO strncpy
  memcpy(buf->mem_ + e.offset_in_block, (const char *)e.bytes.data(),
         e.bytes.size());

  buf->bdwrite();
}

void V6Replay::apply(const LogBlockAlloc &e) {
  auto buf = fs_.bread(e.blockno);
  if (e.zero_on_replay) {
    memset(buf->mem_, 0, SECTOR_SIZE);
    buf->bdwrite();
  }
  freemap_.at(e.blockno) = 0;
}

void V6Replay::apply(const LogBlockFree &e) { freemap_.at(e.blockno) = 1; }

void V6Replay::apply(const LogCommit &e) {}

void V6Replay::apply(const LogRewind &e) {
  // Note:  LogRewind is already handled specially by read_next(),
  // so this method never gets called.  We need the method to exist
  // because of how std::visit function works on std::variant.
}

void V6Replay::read_next(LogEntry *out) {
  auto load = [out, this]() {
    out->load(r_);
    if (out->sequence_ != sequence_)
      throw log_corrupt("bad sequence number");
    ++sequence_;
  };

  load();
  if (out->get<LogRewind>()) {
    r_.seek(hdr_.logstart() * SECTOR_SIZE);
    load();
  }
}

bool V6Replay::check_tx() {
  cleanup _c([this, start = r_.tell()]() { r_.seek(start); });
  lsn_t startseq = sequence_;

  try {
    LogEntry le;
    read_next(&le);
    if (!le.get<LogBegin>())
      throw log_corrupt("no LogBegin");
    lsn_t beginseq = le.sequence_;

    for (;;) {
      read_next(&le);
      if (LogCommit *c = le.get<LogCommit>()) {
        if (c->sequence != beginseq)
          throw log_corrupt("begin/commit sequence mismatch");
        sequence_ = startseq;
        return true;
      }
    }
  } catch (const log_corrupt &e) {
    // Don't reset sequence to ensure checkpoint above existing LSNs
    std::cout << "Reached log end: " << e.what() << std::endl;
    return false;
  }
}

void V6Replay::replay() {
  LogEntry le;
  while (check_tx()) {
    do {
      read_next(&le);
      le.visit([this](const auto &e) { apply(e); });
    } while (!le.get<LogCommit>());
  }

  std::cout << "played log entries " << hdr_.l_sequence << " to " << sequence_
            << std::endl;

  hdr_.l_sequence = sequence_;
  hdr_.l_checkpoint = r_.tell();
  if (pwrite(fs_.fd_, freemap_.data(), freemap_.datasize(),
             hdr_.mapstart() * SECTOR_SIZE) == -1)
    threrror("pwrite");
  // We don't log inode allocations, so just force re-scan
  fs_.superblock().s_fmod = 1;
  fs_.superblock().s_ninode = 0;

  // flush all the changes we just made before updating loghdr to
  // reflect a new checkpoint
  fs_.sync();

  // Now save to update checkpoint (and re-update superblock to show clean)
  fs_.writeblock(&hdr_, fs_.superblock().s_fsize);
  fs_.superblock().s_fmod = 1;
  fs_.unclean_ = false;
}
