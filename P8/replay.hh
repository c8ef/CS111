#pragma once

#include "bitmap.hh"
#include "bufio.hh"
#include "logentry.hh"

struct V6FS;

// This structure exists during log replay, and keeps track of the state
// of the log replay.
struct V6Replay {
  V6FS &fs_;
  FdReader r_;
  lsn_t sequence_; // next sequence number expected
  loghdr hdr_;
  Bitmap freemap_;

  V6Replay(V6FS &fs);

  // The main function that applies the log
  void replay();

  void apply(const LogBegin &);
  void apply(const LogPatch &);
  void apply(const LogBlockAlloc &);
  void apply(const LogBlockFree &);
  void apply(const LogCommit &);
  void apply(const LogRewind &);

  // Read next log entry, bump sequence_, and rewind the FILE
  // pointer if it's LogRewind.
  void read_next(LogEntry *out);

  // Return true if the log is positioned at the beginning of a
  // complete transaction, false otherwise.
  bool check_tx();
};
