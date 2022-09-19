#include <ctime>

#include "blockpath.hh"

void Dirent::set_inum(uint16_t inum) const {
  de_->d_inumber = inum;
  if (!inum)
    de_->name("");
  bp_->fs().patch(*de_);
  dir_->mtouch();
}

void Inode::put() {
  Ref<Buffer> bp = fs().bread(fs().iblock(inum()));
  bp->at<inode>(fs().iindex(inum())) = *this;
  bp->bdwrite();
  dirty_ = logged_ = false;
}

void Inode::set_size(uint32_t sz) {
  i_size0 = sz >> 16;
  i_size1 = sz & 0xffff;
  fs().log_patch(&i_size0, 3);
}

void Inode::clear() {
  truncate(0, DoLog::NOLOG);
  memset(&raw(), 0, sizeof(inode));
  fs().patch(raw());
}

// Free all blocks down to start.  Returns true if all free.
static void free_blocks(BlockPtrArray ba, BlockPath start) {
  for (int i = ba.size(); i-- > start;)
    if (uint16_t bn = ba.at(i)) {
      BlockPath child = start.tail_at(i);
      if (child.height()) {
        free_blocks(ba.fs().bread(bn), child);
        if (!child.is_zero())
          continue;
      }
      ba.fs().bfree(bn);
      ba.set_at(i, 0);
    }
}

#include <iostream> // XXX
// Set the large flag, and allocate an indirect block for the file.
bool Inode::make_large() {
  if (i_mode & ILARG)
    return true;

  Ref<Buffer> bp = fs().balloc(true);
  memcpy(bp->mem_, i_addr, sizeof(i_addr));
  // Log one extra byte (which is harmless in a 512-byte block) to
  // make it easy to differentiate this log entry from a direntryv6.
  fs().log_patch(bp->mem_, sizeof(i_addr) + 1);
  memset(i_addr, 0, sizeof(i_addr));
  i_addr[0] = bp->blockno();
  i_mode |= ILARG;
  fs().patch(raw());
  return true;
}

// Clear the large flag, and move the first 8 block pointers directly
// into the inode's i_addr array.
void Inode::make_small(DoLog dolog) {
  if (!(i_mode & ILARG))
    return;

  uint16_t addrs[IADDR_SIZE];
  if (i_addr[0]) {
    Ref<Buffer> ibp = fs().bread(i_addr[0]);
    memcpy(addrs, ibp->mem_, sizeof(addrs));
    memset(ibp->mem_, 0, sizeof(addrs));
    ibp->bdwrite();
  } else
    memset(addrs, 0, sizeof(addrs));

  free_blocks(Ref{this}, blockno_path(i_mode, IADDR_SIZE));
  fs().bfree(i_addr[0]);
  memcpy(i_addr, addrs, sizeof(addrs));
  i_mode &= ~ILARG;
  if (dolog == DoLog::LOG)
    fs().patch(raw());
}

void Inode::truncate(uint32_t sz, DoLog dolog) {
  bool converted_to_small = false;
  if (sz > MAX_FILE_SIZE)
    throw resource_exhausted("truncate: maximum file size exceeded", -EFBIG);
  if (sz <= IADDR_SIZE * SECTOR_SIZE) {
    make_small(DoLog::NOLOG);
    converted_to_small = true;
  }

  BlockPath pth = sentinel_path(i_mode, sz);
  free_blocks(Ref{this}, pth);

  if (dolog == DoLog::NOLOG)
    size(sz);
  else if (!converted_to_small)
    set_size(sz);
  else
    fs().patch(raw());
}

Ref<Buffer> Inode::getblock(uint16_t blockno, bool allocate) {
  if (allocate && blockno >= IADDR_SIZE)
    make_large();
  assert(!allocate || !fs().log_ || fs().log_->in_tx_);

  Ref<Buffer> bp;
  BlockPtrArray ba(this);

  for (BlockPath idx = blockno_path(i_mode, blockno); idx.height();
       idx = idx.tail()) {
    if (uint16_t bn = ba.at(idx); !bn) {
      if (!allocate)
        return nullptr;
      bp = fs().balloc(idx.height() > 1 || (i_mode & IFMT) == IFDIR);
      bn = bp->blockno();
      ba.set_at(idx, bp->blockno());
    } else
      bp = fs().bread(bn);
    ba = bp;
  }

  return bp;
}

Dirent Inode::lookup(std::string_view name) {
  if ((i_mode & IFMT) != IFDIR)
    throw std::logic_error("Inode::lookup on non-directory");
  Cursor c(this);
  while (direntv6 *p = c.next<direntv6>())
    if (p->d_inumber && p->name() == name)
      return {this, c.bp_, p};
  return {};
}

// Look up a filename if this inode is a directory, and if the
// filename doesn't exist create a directory entry for it.
Dirent Inode::create(std::string_view name) {
  if ((i_mode & IFMT) != IFDIR)
    throw std::logic_error("Inode::create on non-directory");
  Dirent spare;
  Cursor c(this);
  while (direntv6 *p = c.next<direntv6>())
    if (p->name() == name)
      return {this, c.bp_, p};
    else if (!spare && p->d_inumber == 0)
      spare = {this, c.bp_, p};
  if (!spare) {
    direntv6 *p = c.writenext<direntv6>();
    p->d_inumber = 0;
    spare = {this, c.bp_, p};
  }
  spare.name(name);
  // We don't bother marking the directory block dirty or updating
  // the directory's mtime, as set_inum() already does these.
  return spare;
}

void Inode::atouch() {
  if (!fs().readonly_) {
    // Don't bother logging updates to the atime
    atime(std::time(nullptr));
    mark_dirty();
  }
}

void Inode::mtouch(DoLog dolog) {
  mtime(std::time(nullptr));
  if (dolog == DoLog::LOG)
    fs().patch(i_mtime);
  else
    mark_dirty();
}
