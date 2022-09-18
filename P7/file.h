#ifndef _FILE_H_
#define _FILE_H_

#include "unixfilesystem.h"

/**
 * Fetches the specified file block from the specified inode.
 * Returns the number of valid bytes in the block (which will be the
 * same as the sector size, except for the last block of a file)
 * or -1 on error.
 */
int file_getblock(struct unixfilesystem *fs, int inumber, int blockNo,
                  void *buf);

#endif // _FILE_H_
