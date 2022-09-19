#include "blockpath.hh"

// First block number that requires a double-indirect block
static constexpr uint16_t FIRST_DINDR_BLK = (IADDR_SIZE - 1) * INDBLK_SIZE;

BlockPath blockno_path(uint16_t mode, uint32_t bn) {
  if ((mode & ILARG) == 0) {
    if (bn > IADDR_SIZE)
      throw std::length_error("BlockPath: small-file length exceeded");
    return BlockPath::make(bn);
  }

  if (bn < FIRST_DINDR_BLK)
    return BlockPath::make(bn / INDBLK_SIZE, bn % INDBLK_SIZE);
  bn -= FIRST_DINDR_BLK;
  return BlockPath::make(IADDR_SIZE - 1, bn / INDBLK_SIZE, bn % INDBLK_SIZE);
}

BlockPath sentinel_path(uint16_t mode, uint32_t size) {
  const uint32_t bn = size / SECTOR_SIZE + (size % SECTOR_SIZE != 0);
  if (!(mode & ILARG))
    return blockno_path(mode, std::min<uint32_t>(bn, IADDR_SIZE));
  return blockno_path(mode, std::min<uint32_t>(0x10000, bn));
}

uint16_t blockpath_no(BlockPath pth) {
  switch (pth.height()) {
  case 1:
    if (pth < IADDR_SIZE)
      return pth;
    break;
  case 2:
    if (pth < IADDR_SIZE - 1)
      return INDBLK_SIZE * pth + pth.tail();
    break;
  case 3:
    if (pth == IADDR_SIZE - 1) {
      pth = pth.tail();
      return FIRST_DINDR_BLK + INDBLK_SIZE * pth + pth.tail();
    }
    break;
  }
  throw std::logic_error("blockpath_no: invalid path");
}

bool BlockPtrArray::check(bool dbl_indir) {
  for (unsigned i = 0, e = size(); i < e; ++i)
    if (uint16_t bn = data()[i])
      if (fs().badblock(bn)
          // The maxiumum file size is 2^{24}-1 bytes, or 2^{16}
          // sectors, so the last (IADDR_SIZE-1) block numbers
          // in a double-indirect block must be all zeros.
          || (dbl_indir && i >= INDBLK_SIZE - (IADDR_SIZE - 1)))
        return false;
  return true;
}
