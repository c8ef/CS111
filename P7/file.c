#include <assert.h>
#include <stdio.h>

#include "diskimg.h"
#include "file.h"
#include "inode.h"

int file_getblock(struct unixfilesystem *fs, int inumber, int blockNum,
                  void *buf) {
  struct inode i_node;
  inode_iget(fs, inumber, &i_node);
  int phy_block = inode_indexlookup(fs, &i_node, blockNum);
  int fize_size = inode_getsize(&i_node);
  int ret = fize_size / DISKIMG_SECTOR_SIZE == blockNum
                ? fize_size % DISKIMG_SECTOR_SIZE
                : DISKIMG_SECTOR_SIZE;
  diskimg_readsector(fs->dfd, phy_block, buf);
  return ret;
}
