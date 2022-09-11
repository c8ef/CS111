#!/usr/bin/python3

# Prints info to stdout about file descriptors that are currently open.

import os
import sys

fds = {}

for i in range(0, 10):
   fd1, fd2 = os.pipe()
   fds[fd1] = "yes"
   fds[fd2] = "yes"

open_fds = ""
for i in range(0, 20):
    if i in fds:
        continue
    if len(open_fds) != 0:
        open_fds += ", "
    open_fds += str(i)

print("Open file descriptors: %s" % (open_fds))
