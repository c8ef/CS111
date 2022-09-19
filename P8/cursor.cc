#include "v6fs.hh"

void Cursor::seek(uint32_t pos) {
  if (pos > MAX_FILE_SIZE)
    throw resource_exhausted("seek: maximum file size exceeded", -EFBIG);
  if ((pos - 1) / SECTOR_SIZE != (pos_ - 1) / SECTOR_SIZE)
    bp_ = nullptr;
  pos_ = pos;
}

void *Cursor::readref(size_t n) {
  if (n == 0)
    return nullptr;
  if (n > SECTOR_SIZE || (pos_ + n - 1) / SECTOR_SIZE != pos_ / SECTOR_SIZE)
    throw std::logic_error("Cursor::readref: alignment error");
  uint32_t filesize = ip_->size();
skip_sparse_block:
  if (pos_ >= filesize || n > filesize - pos_)
    return nullptr;
  uint16_t offset = pos_ % SECTOR_SIZE;
  if (!bp_ || offset == 0) {
    bp_ = ip_->getblock(pos_ / SECTOR_SIZE);
    if (!bp_) {
      pos_ = pos_ - offset + SECTOR_SIZE;
      goto skip_sparse_block;
    }
  }
  pos_ += n;
  return &bp_->mem_[offset];
}

void *Cursor::writeref(size_t n) {
  if (n == 0)
    return nullptr;
  if (n > SECTOR_SIZE || (pos_ + n - 1) / SECTOR_SIZE != pos_ / SECTOR_SIZE)
    throw std::logic_error("Cursor::readref: alignment error");
  if (n > MAX_FILE_SIZE - pos_)
    throw resource_exhausted("writeref: maximum file size exceeded", -EFBIG);

  bp_ = ip_->getblock(pos_ / SECTOR_SIZE, true);
  if (!bp_)
    return nullptr;
  void *res = &bp_->mem_[pos_ % SECTOR_SIZE];
  pos_ += n;
  if (pos_ > ip_->size()) {
    ip_->set_size(pos_);
    ip_->mtouch();
  }
  bp_->bdwrite();
  return res;
}

int Cursor::read(void *_buf, size_t n) {
  char *buf = static_cast<char *>(_buf);
  int nread = 0;
  uint32_t filesize = ip_->size();
  while (n > 0 && pos_ < filesize) {
    size_t start = pos_ % SECTOR_SIZE;
    if (start == 0)
      bp_ = nullptr;
    size_t to_read = SECTOR_SIZE - start;
    if (to_read > n)
      to_read = n;
    if (uint32_t remain = filesize - pos_; to_read > remain)
      to_read = remain;
    if (!bp_)
      bp_ = ip_->getblock(pos_ / SECTOR_SIZE);
    if (bp_)
      memcpy(buf, bp_->mem_ + start, to_read);
    else
      memset(buf, '\0', to_read);
    nread += to_read;
    buf += to_read;
    n -= to_read;
    pos_ += to_read;
  }
  if (nread > 0)
    ip_->atouch();
  if (pos_ % SECTOR_SIZE == 0)
    bp_ = 0;
  return nread;
}

int Cursor::write(const void *_buf, size_t n) {
  const char *buf = static_cast<const char *>(_buf);

  if (n > MAX_FILE_SIZE - pos_)
    throw resource_exhausted("write: maximum file size exceeded", -EFBIG);

  int nwritten = 0;
  while (n > 0) {
    size_t start = pos_ % SECTOR_SIZE;
    if (start == 0)
      bp_ = nullptr;
    size_t to_write = SECTOR_SIZE - start;
    if (to_write > n)
      to_write = n;
    if (!bp_ && !(bp_ = ip_->getblock(pos_ / SECTOR_SIZE, true)))
      break;
    memcpy(bp_->mem_ + start, buf, to_write);
    pos_ += to_write;
    nwritten += to_write;
    buf += to_write;
    n -= to_write;
    if (to_write)
      bp_->bdwrite();
  }
  if (nwritten > 0) {
    if (pos_ > ip_->size()) {
      ip_->set_size(pos_);
      ip_->mtouch();
    } else
      ip_->mtouch(DoLog::NOLOG);
  }
  if (pos_ % SECTOR_SIZE == 0)
    bp_ = 0;
  return n == 0 ? nwritten : -1;
}
