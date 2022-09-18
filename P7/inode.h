#ifndef _INODE_H
#define _INODE_H

#include "unixfilesystem.h"

/**
 * Given the i-number of a file (inumber),this function fetches from
 * disk the inode for that file and stores the inode contents at *inp.
 * Returns 0 on success, -1 on error.
 */
int inode_iget(struct unixfilesystem *fs, int inumber, struct inode *inp);

/**
 * Given the logical index of a block in a file (blockNum; the first block
 * is index 0), returns the physical block number on disk where that block
 * is stored, if there is one. If that logical block number does not currently
 * exist within the file, then  -1 is returned. Inp points to the inode for
 * the file.
 */
int inode_indexlookup(struct unixfilesystem *fs, struct inode *inp,
                      int blockNum);

/**
 * Given an inode, this function computes the size of its file from the
 * size0 and size1 fields in the inode.
 */
int inode_getsize(struct inode *inp);

#endif // _INODE_
