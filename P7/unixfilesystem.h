#ifndef _UNIXFILESYSTEM_H_
#define _UNIXFILESYSTEM_H_

/**
 * Include the definitions taken from the Unix sources.
 */

#include "direntv6.h"
#include "filsys.h"
#include "ino.h"

/**
 * The layout of the Unix disk looked as follows:
 * ----------------------------------------------
 * Block 0 - The bootblock. The first 16-bit word of this sector should be 0407.
 * Block 1 - The superblock (struct filsys as defined in filsys.h)
 * Block 2 - The start of the inode area of the disk.
 *    The s_isize field within the superblock tells how many blocks of
 *    inodes there are.
 * Block 2 + s_isize : The rest of the blocks on disk.
 */

#define BOOTBLOCK_SECTOR 0
#define SUPERBLOCK_SECTOR 1
#define INODE_START_SECTOR 2
#define ROOT_INUMBER 1
#define BOOTBLOCK_MAGIC_NUM 0407

struct unixfilesystem {
  int dfd;                  // Handle from the diskimg module to read
                            // the disk image.
  struct filsys superblock; // The superblock read from the disk image.
};

struct unixfilesystem *unixfilesystem_init(int fd);

#endif // _UNIXFILESYSTEM_H_
