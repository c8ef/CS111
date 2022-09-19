#include "v6fs.hh"

void Buffer::bwrite() {
  assert(!logged_ || V6Log::le(lsn_, fs().log_->committed_));

  fs().writeblock(mem_, blockno());
  initialized_ = true;
  dirty_ = logged_ = false;
}
