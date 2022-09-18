
#include "pathname.h"
#include "directory.h"
#include "diskimg.h"
#include "inode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int pathname_lookup(struct unixfilesystem *fs, const char *pathname) {
  // Remove the placeholder code below and add your implementation.
  fprintf(stderr, "pathname_lookup(path=%s) unimplemented.  Returing -1.\n",
          pathname);
  return -1;
}
