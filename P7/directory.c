#include "directory.h"
#include "direntv6.h"
#include "diskimg.h"
#include "file.h"
#include "inode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define NUM_DIRENTS_PER_BLOCK (DISKIMG_SECTOR_SIZE / sizeof(struct direntv6))
#define MAX_COMPONENT_LENGTH sizeof(dirEnt->d_name)

int directory_findname(struct unixfilesystem *fs, const char *name,
                       int dirinumber, struct direntv6 *dirEnt) {
  struct inode ino;
  if (inode_iget(fs, dirinumber, &ino) < 0)
    return -1;
  if ((ino.i_mode & IFDIR) == 0)
    return -1;
  int dir_size;
  if ((dir_size = inode_getsize(&ino)) <= 0)
    return -1;

  int total_block_num = (dir_size - 1) / DISKIMG_SECTOR_SIZE + 1;
  for (int i = 0; i < total_block_num; i++) {
    struct direntv6 entries[NUM_DIRENTS_PER_BLOCK];
    int valid_bytes = file_getblock(fs, dirinumber, i, entries);
    if (valid_bytes < 0)
      return -1;
    int total_entry_num = valid_bytes / sizeof(struct direntv6);
    for (int j = 0; j < total_entry_num; j++) {
      if (strncmp(entries[j].d_name, name, MAX_COMPONENT_LENGTH) == 0) {
        *dirEnt = entries[j];
        return 0;
      }
    }
  }
  return -1;
}
