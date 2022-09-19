#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "diskimg.h"
#include "inode.h"

#define INODES_PER_BLOCK (DISKIMG_SECTOR_SIZE / sizeof(struct inode))
#define NUM_BLOCK_NUMS_PER_BLOCK (DISKIMG_SECTOR_SIZE / sizeof(uint16_t))

int inode_iget(struct unixfilesystem *fs, int inumber, struct inode *inp) {
  int sector_num = (inumber - 1) / INODES_PER_BLOCK + 2,
      in_sec_num = (inumber - 1) % INODES_PER_BLOCK;

  struct inode inode_arr[INODES_PER_BLOCK];
  diskimg_readsector(fs->dfd, sector_num, inode_arr);
  *inp = inode_arr[in_sec_num];

  return 0;
}

int inode_indexlookup(struct unixfilesystem *fs, struct inode *inp,
                      int blockNum) {
  // use i_mode detect large file
  if (!(inp->i_mode & ILARG))
    return inp->i_addr[blockNum] != 0 ? inp->i_addr[blockNum] : -1;
  uint16_t buf[NUM_BLOCK_NUMS_PER_BLOCK];
  if (blockNum < 7 * NUM_BLOCK_NUMS_PER_BLOCK) {
    int indirBlockNum = blockNum / NUM_BLOCK_NUMS_PER_BLOCK;
    diskimg_readsector(fs->dfd, inp->i_addr[indirBlockNum], buf);
    return buf[blockNum % NUM_BLOCK_NUMS_PER_BLOCK] != 0
               ? buf[blockNum % NUM_BLOCK_NUMS_PER_BLOCK]
               : -1;
  } else {
    int indirBlockNum =
        (blockNum - 7 * NUM_BLOCK_NUMS_PER_BLOCK) / NUM_BLOCK_NUMS_PER_BLOCK;
    diskimg_readsector(fs->dfd, inp->i_addr[7], buf);
    diskimg_readsector(fs->dfd, buf[indirBlockNum], buf);
    return buf[blockNum % NUM_BLOCK_NUMS_PER_BLOCK] != 0
               ? buf[blockNum % NUM_BLOCK_NUMS_PER_BLOCK]
               : -1;
  }
}

int inode_getsize(struct inode *inp) {
  return ((inp->i_size0 << 16) | inp->i_size1);
}
