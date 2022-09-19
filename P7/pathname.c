#include "pathname.h"
#include "directory.h"
#include "diskimg.h"
#include "inode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int pathname_lookup(struct unixfilesystem *fs, const char *pathname) {
  if (strcmp(pathname, "/") == 0)
    return 1;
  struct direntv6 dir;
  char *p = pathname + 1, *q = pathname + 1;
  int dir_num = 1;
  while (1) {
    while (*q && *q != '/')
      q++;
    char dir_buf[14] = "";
    strncpy(dir_buf, p, q - p);

    if (directory_findname(fs, dir_buf, dir_num, &dir) == -1)
      return -1;
    if (!*q) {
      return dir.d_inumber;
    }
    p = q + 1;
    q = p;
    dir_num = dir.d_inumber;
  }
}