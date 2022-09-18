#include <assert.h>
#include <stdio.h>

#include "diskimg.h"
#include "inode.h"
#include <stdbool.h>

#define INODES_PER_BLOCK (DISKIMG_SECTOR_SIZE / sizeof(struct inode))
#define NUM_BLOCK_NUMS_PER_BLOCK (DISKIMG_SECTOR_SIZE / sizeof(uint16_t))

int inode_iget(struct unixfilesystem *fs, int inumber, struct inode *inp) {
  // Remove the placeholder code below and add your implementation.
  fprintf(stderr, "inode_get(inumber=%d) not implemented, returning -1\n",
          inumber);
  return -1;
}

int inode_indexlookup(struct unixfilesystem *fs, struct inode *inp,
                      int blockNum) {
  // Remove the placeholder code below and add your implementation.
  fprintf(stderr,
          "inode_indexlookup(blockNum=%d) not implemented, "
          "returning -1\n",
          blockNum);
  return -1;
}

int inode_getsize(struct inode *inp) {
  return ((inp->i_size0 << 16) | inp->i_size1);
}
