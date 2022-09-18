#include "directory.h"
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
  // Remove the following placeholder implementation and replace
  // with a real implementation.
  fprintf(stderr, "directory_lookupname(name=%s dirinumber=%d)\n", name,
          dirinumber);
  return -1;
}
