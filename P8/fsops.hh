#pragma once

#include "v6fs.hh"

// Perm checker returns a 3-bit mask of allowed permissions on an
// inode (4=read, 2=write, 1=execute).
using inode_permissions = std::function<int(const Inode *)>;

// Vacuously allways returns full permissions.
int null_inode_permissions(const Inode *);

// If result is start, or ends with "..", okay to Dirent with "." file name:
constexpr int ND_DOT_OK = 0x1;
// Create directory entry (with inum 0) if doesn't exist:
constexpr int ND_CREATE = 0x2;
// With ND_CREATE, file name must not already exist:
constexpr int ND_EXCLUSIVE = 0x4;
// Require write permission to director (for deleting links):
constexpr int ND_DIRWRITE = 0x8;

// If start is nullptr, starts at root directory.
int fs_named(Dirent *out, Ref<Inode> start, std::string path, int flags,
             inode_permissions = null_inode_permissions);

// Functions that allocate an inode take an inode_initializer to set
// up permissions and such inside the log transaction.
using inode_initializer = std::function<void(inode *)>;

int fs_mknod(Dirent where, inode_initializer);
int fs_mkdir(Dirent where, inode_initializer);
int fs_rmdir(Dirent where);
int fs_link(Dirent oldde, Dirent newde);
int fs_unlink(Dirent where);
int fs_num_free_inodes(V6FS &fs);
int fs_num_free_blocks(V6FS &fs);

// Get a copy of the freemap.  If the FS in is logging mode, copy the
// in-memory bitmap.  Otherwise if the superblock supports logging,
// read the log area from disk even if the V6FS isn't currently in
// logging mode.  Otherwise, if free blocks are stored in the old 1975
// 100-wide linked-list format, traverse the list to build the
// freemap.
Bitmap fs_freemap(V6FS &fs);
