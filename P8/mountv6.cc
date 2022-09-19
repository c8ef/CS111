#define FUSE_USE_VERSION 31

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "fsops.hh"

FScache cache;
V6FS *fs;

static constexpr fuse_fill_dir_flags FILLDIR_FLAGS_NONE(fuse_fill_dir_flags(0));

static struct options {
  int show_help;
  int checkuid;
  int create_journal;
  int force;
  int suppress_commit;
} options;

#define OPTION(t, p)                                                           \
  { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
    OPTION("--suppress-commit", suppress_commit),
    OPTION("--checkuid", checkuid),
    OPTION("--force", force),
    OPTION("-h", show_help),
    OPTION("--help", show_help),
    OPTION("-j", create_journal),
    FUSE_OPT_END};

static bool root_user() {
  return !options.checkuid || !fuse_get_context()->uid;
}

static int file_owner(const Ref<Inode> &ip) {
  if (!ip)
    return -ENOENT;
  if (root_user() || (fuse_get_context()->uid & 0xff) == ip->i_uid)
    return 0;
  return -EPERM;
}

static Ref<Inode> get_inode(const char *path, fuse_file_info *fi = nullptr) {
#if 0
    if (fi)
        printf("get_inode: path == \"%s\", fh == %ld\n", path, fi->fh);
    else
        printf("get_inode: path == \"%s\", fi == NULL\n", path);
#endif
  if (fi && fi->fh)
    return fs->iget(fi->fh);
  else if (Ref<Inode> ip = fs->namei(path)) {
    if (fi)
      fi->fh = ip->inum();
    return ip;
  }
  return nullptr;
}

static int flags_to_mode(int flags) {
  switch (flags & O_ACCMODE) {
  case O_RDONLY:
    return 4;
  case O_WRONLY:
    return 2;
  case O_RDWR:
    return 6;
  }
  return 7; // fail secure
}

// Check access, ignoring all but lower 8 bits of uid...
static int check_access(Ref<Inode> ip, int mode) {
  if (!ip)
    return -ENOENT;
  fuse_context *fc = fuse_get_context();
  int uid = fc->uid & 0xff, gid = fc->gid & 0xff;
  if (root_user())
    return 0;
  if (ip->i_uid == uid)
    mode <<= 6;
  else if (ip->i_gid == gid)
    mode <<= 3;
  return (ip->i_mode & mode) == mode ? 0 : -EACCES;
}

// Check access, ignoring all but lower 8 bits of uid...
static int get_perms(const Inode *ip) {
  if (root_user())
    return 7;
  fuse_context *fc = fuse_get_context();
  int uid = fc->uid & 0xff, gid = fc->gid & 0xff;
  if (ip->i_uid == uid)
    return ip->i_mode >> 6 & 7;
  else if (ip->i_gid == gid)
    return ip->i_mode >> 3 & 7;
  return ip->i_mode & 7;
}

// Get a directory entry for adding or removing links.  Name must not
// be "." or ".." and directory must be writable or it returns an
// error.
static int get_dirent(Dirent *out, const char *path, int flags) try {
  Ref<Inode> root = fs->iget(ROOT_INUMBER);
  return fs_named(out, root, path, flags, get_perms);
} catch (const resource_exhausted &e) {
  return e.error;
}

static int get_attr(Ref<Inode> ip, struct stat *st) {
  memset(st, 0, sizeof(*st));
  if (!(ip->i_mode & IALLOC)) {
    fprintf(stderr, "Invalid unallocated inode %d\n", ip->inum());
    return -EIO;
  }

  switch (ip->i_mode & IFMT) {
  case IFDIR:
    st->st_mode = S_IFDIR;
    break;
  case IFCHR:
    st->st_mode = S_IFCHR;
    break;
  case IFBLK:
    st->st_mode = S_IFBLK;
    break;
  default:
    st->st_mode = S_IFREG;
    break;
  }
  st->st_mode |= ip->i_mode & 07777;
  st->st_ino = ip->inum();
  st->st_nlink = ip->i_nlink;
  st->st_uid = ip->i_uid;
  st->st_gid = ip->i_gid;
  st->st_size = ip->size();
  st->st_blksize = SECTOR_SIZE;
  // XXX blocks shouldn't count gaps in sparse files
  st->st_blocks = (st->st_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
  st->st_atime = ip->atime();
  st->st_mtime = ip->mtime();
  st->st_ctime = ip->mtime();
  if (st->st_mode & (IFCHR | IFBLK))
    st->st_rdev = makedev(ip->major(), ip->minor());
  return 0;
}

static void *v6_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
  cfg->kernel_cache = 1;
  cfg->use_ino = 1;
  return nullptr;
}

static int v6_getattr(const char *path, struct stat *st,
                      struct fuse_file_info *fi) {
  Ref<Inode> ip = get_inode(path, fi);
  if (!ip)
    return -ENOENT;
  return get_attr(ip, st);
}

static int v6_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi,
                      enum fuse_readdir_flags flags) {
  Ref<Inode> ip = get_inode(path, fi);
  if (!ip)
    return -ENOENT;
  Cursor c(ip);
  c.seek(offset - (offset % sizeof(direntv6)));

  for (direntv6 *d = c.next<direntv6>(); d; d = c.next<direntv6>()) {
    if (!d->d_inumber)
      continue;
    if (filler(buf, std::string(d->name()).c_str(), nullptr, c.tell(),
               FILLDIR_FLAGS_NONE))
      break;
  }
  return 0;
}

static int v6_open(const char *path, struct fuse_file_info *fi) {
  Tx _tx = fs->begin();
  Ref<Inode> ip = get_inode(path, fi);
  if (int err = check_access(ip, flags_to_mode(fi->flags)))
    return err;
  if (fi->flags & O_TRUNC) {
    if ((ip->i_mode & IFMT) == IFREG) {
      ip->truncate();
      ip->mtouch();
    } else
      return -EINVAL;
  }
  return 0;
}

static int v6_truncate(const char *path, off_t size,
                       struct fuse_file_info *fi) {
  Tx _tx = fs->begin();
  Ref<Inode> ip = get_inode(path, fi);
  if (int err = check_access(ip, 2))
    return err;
  ip->truncate(std::min<off_t>(size, MAX_FILE_SIZE));
  return 0;
}

static int v6_utimens(const char *path, const struct timespec tv[2],
                      struct fuse_file_info *fi) {
  Ref<Inode> ip = get_inode(path, fi);
  if (int err = check_access(ip, 2))
    return err;
  Tx _tx = fs->begin();
  if (tv[0].tv_nsec == UTIME_NOW) {
    ip->atouch();
    if (tv[1].tv_nsec == UTIME_NOW) {
      // make times identical instead of re-reading system time
      ip->mtime(ip->atime());
      fs->log_patch(&ip->i_atime, 8);
      return 0;
    }
  } else if (tv[0].tv_nsec != UTIME_OMIT)
    ip->atime(tv[0].tv_sec);
  if (tv[1].tv_nsec == UTIME_NOW)
    ip->mtime(time(NULL));
  else if (tv[1].tv_nsec != UTIME_OMIT)
    ip->mtime(tv[1].tv_sec);
  fs->log_patch(&ip->i_atime, 8);
  return 0;
}

static int v6_chown(const char *path, uid_t uid, gid_t gid,
                    struct fuse_file_info *fi) {
  Ref<Inode> ip = get_inode(path, fi);
  if (int err = file_owner(ip))
    return err;
  Tx _tx = fs->begin();
  if (uid != uid_t(-1)) {
    if (!root_user())
      ip->i_mode &= ~04000;
    ip->i_uid = uid;
  }
  if (gid != uid_t(-1)) {
    if (!root_user() && gid != (fuse_get_context()->gid & 0xff))
      ip->i_mode &= ~02000;
    ip->i_gid = gid;
  }
  fs->log_patch(&ip->i_uid, 2);
  ip->mtouch();
  return 0;
}

static int v6_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
  Ref<Inode> ip = get_inode(path, fi);
  if (int err = file_owner(ip))
    return err;
  Tx _tx = fs->begin();
  // Don't allow setgid if not in group
  if ((mode & 02000) && !root_user() &&
      (fuse_get_context()->gid & 0xff) != ip->i_gid)
    mode &= ~02000;
  fs->patch(ip->i_mode, (ip->i_mode & ~07777) | (mode & 07777));
  ip->mtouch();
  return 0;
}

static int v6_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
  Ref<Inode> ip = get_inode(path, fi);
  if (!ip)
    return -ENOENT;

  Cursor c(ip);
  c.seek(offset);
  ip->atouch();
  return c.read(buf, size);
}

static int v6_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) try {
  Ref<Inode> ip = get_inode(path, fi);
  if (!ip)
    return -ENOENT;

  Tx _tx = fs->begin();
  Cursor c(ip);
  c.seek(offset);
  // Since write's aren't metadata, don't bother logging mtime
  ip->mtouch(DoLog::NOLOG);
  return c.write(buf, size);
} catch (const resource_exhausted &e) {
  return e.error;
}

static int v6_mknod(const char *path, mode_t mode, dev_t dev) {
  uint16_t newmode = (mode & 07777) | IALLOC;
  switch (mode & S_IFMT) {
  case S_IFBLK:
    newmode |= IFBLK;
    break;
  case S_IFCHR:
    newmode |= IFCHR;
    break;
  default:
    return -EINVAL;
  }
  if (major(dev) > 0xff || minor(dev) > 0xff)
    return -EINVAL;
  if (!root_user())
    return -EPERM;

  Tx _tx = fs->begin();
  Dirent de;
  if (int err = get_dirent(&de, path, ND_CREATE | ND_EXCLUSIVE))
    return err;

  return fs_mknod(de, [newmode, dev](inode *ip) {
    ip->i_mode = newmode;
    ip->major() = major(dev);
    ip->minor() = minor(dev);
  });
}

static int v6_create(const char *path, mode_t mode, fuse_file_info *fi) {
  Tx _tx = fs->begin();
  Dirent de;
  if (int err = get_dirent(&de, path, ND_CREATE))
    return err;

  if (de.inum()) {
    fi->fh = de.inum();
    return 0;
  }

  if (int err = fs_mknod(de, [mode](inode *ip) { ip->i_mode |= mode & 07777; }))
    return err;
  fi->fh = de.inum();
  return 0;
}

static int v6_unlink(const char *path) {
  Dirent de;
  if (int err = get_dirent(&de, path, ND_DIRWRITE))
    return err;
  return fs_unlink(de);
}

static int v6_mkdir(const char *path, mode_t mode) {
  Tx _tx = fs->begin();
  Dirent de;
  if (int err = get_dirent(&de, path, ND_CREATE | ND_EXCLUSIVE))
    return err;
  return fs_mkdir(de, [mode](inode *ip) {
    ip->i_mode = (mode & 07777) | IFDIR | IALLOC;
    if (!root_user()) {
      fuse_context *fc = fuse_get_context();
      ip->i_uid = fc->uid;
      ip->i_gid = fc->gid;
    }
  });
}

static int v6_rmdir(const char *path) {
  Dirent de;
  if (int err = get_dirent(&de, path, ND_DIRWRITE))
    return err;
  return fs_rmdir(de);
}

static int v6_link(const char *oldpath, const char *newpath) {
  Dirent oldde, newde;
  if (int err = get_dirent(&oldde, oldpath, ND_DIRWRITE))
    return err;
  Tx _tx = fs->begin();
  if (int err =
          get_dirent(&newde, newpath, ND_CREATE | ND_EXCLUSIVE | ND_DIRWRITE))
    return err;
  return fs_link(oldde, newde);
}

static int v6_rename(const char *oldpath, const char *newpath,
                     unsigned int flags) {
  if (flags)
    return -EINVAL;

  Dirent oldde;
  if (int err = get_dirent(&oldde, oldpath, ND_DIRWRITE))
    return err;

  Tx _tx = fs->begin();
  Dirent newde;
  if (int err = get_dirent(&newde, newpath, ND_CREATE))
    return err;

  if (newde.inum()) {
    Ref<Inode> ip = fs->iget(newde.inum());
    if (ip->i_nlink > 1) {
      --ip->i_nlink;
      fs->patch(ip->i_nlink);
      ip->mtouch();
    } else {
      ip->clear();
      fs->ifree(ip->inum());
    }
  }
  Ref<Inode> ip = fs->iget(oldde.inum());
  newde.set_inum(ip->inum());
  oldde.set_inum(0);
  ip->mtouch();

  return 0;
}

static int v6_statfs(const char *path, struct statvfs *sfs) {
  filsys &sb = fs->superblock();
  memset(sfs, 0, sizeof(*sfs));
  sfs->f_bsize = SECTOR_SIZE;
  sfs->f_blocks = sb.s_fsize - sb.datastart();
  sfs->f_bavail = sfs->f_bfree = fs_num_free_blocks(*fs);
  sfs->f_files = sb.s_isize * INODES_PER_BLOCK;
  sfs->f_favail = sfs->f_ffree = fs_num_free_inodes(*fs);
  return 0;
}

static const fuse_operations v6_oper = []() {
  fuse_operations ops{};
  ops.getattr = v6_getattr;
  ops.open = v6_open;
  ops.read = v6_read;
  ops.write = v6_write;
  ops.readdir = v6_readdir;
  ops.init = v6_init;
  ops.create = v6_create;
  ops.unlink = v6_unlink;
  ops.mkdir = v6_mkdir;
  ops.rmdir = v6_rmdir;
  ops.link = v6_link;
  ops.truncate = v6_truncate;
  ops.utimens = v6_utimens;
  ops.chown = v6_chown;
  ops.chmod = v6_chmod;
  ops.mknod = v6_mknod;
  ops.rename = v6_rename;
  ops.statfs = v6_statfs;
  return ops;
}();

static void usage(const char *progname) {
  printf("usage: %s [options] <fs-image> <mountpoint>\n\n", progname);
  printf("File-system specific options:\n"
         "    -j                  Create journal if not already journaling\n"
         "    --checkuid          Use low byte of uid for access control\n"
         "    --force             Mount a dirty file system (beware!)\n"
         "    --suppress-commit   Write metadata to log but not file system\n"
         "                        (only for generating test cases!)\n"
         " watch all hell break loose\n"
         "\n");
}

void mount_cleanup(std::string progdir, const char *mountpoint) {
  pid_t pid = fork();
  if (!pid) {
    close(2);
    open("/dev/null", O_WRONLY);
    execlp("fusermount", "fusermount", "-u", mountpoint, nullptr);
    perror("fusermount");
    _exit(1);
  }
  waitpid(pid, nullptr, 0);

  // Now use a pipe to wait for parent to die, detect it by EOF, and
  // unmount file system.
  int fds[2];
  if (pipe(fds) == -1)
    return;

  pid = fork();
  if (!pid) {
    close(fds[1]);

    // detatch from parent
    if (fork())
      _exit(0);
    setsid();

    if (fds[0] != 0) {
      dup2(fds[0], 0);
      close(fds[0]);
    }
    std::string fcpath = progdir + "/fusecleanup";
    execl(fcpath.c_str(), fcpath.c_str(), mountpoint, nullptr);
    // We try to do this in a separate program so that pkill won't
    // kill it, but we fall back to running fusermount here.
    perror(fcpath.c_str());
    char c;
    read(0, &c, 1);
    execlp("fusermount", "fusermount", "-zu", mountpoint, nullptr);
    perror("fusermount");
    _exit(1);
  }
  close(fds[0]);
  fcntl(F_SETFD, fds[1], FD_CLOEXEC);
  waitpid(pid, nullptr, 0);
}

int main(int argc, char **argv) {
  int ret;
  struct fuse_args args;
  const char *image = nullptr;
  const char *mountpoint = nullptr;

  auto [progdir, progname] = splitpath(argv[0]);

  memset(&args, 0, sizeof(args));
  for (int i = 0; i < argc; ++i)
    if (i > 0 && i == argc - 2 && *argv[i] != '-')
      image = argv[i];
    else {
      if (i > 0 && i == argc - 1 && *argv[i] != '-')
        mountpoint = argv[i];
      fuse_opt_add_arg(&args, argv[i]);
    }

  fuse_opt_add_arg(&args, "-f"); // don't fork
  fuse_opt_add_arg(&args, "-s"); // single-threaded

  if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
    return 1;

  if (options.show_help || !image) {
    options.show_help = 1;
    usage(progname.c_str());
    fuse_opt_add_arg(&args, "--help");
    // Clearing argv[0] prevents fuse from printing a second usage
    // line, since we allready printed one in usage()
    args.argv[0][0] = '\0';
  }

  if (image) {
    int flags = 0;
    if (!options.force)
      flags |= V6FS::V6_MUST_BE_CLEAN;
    if (options.create_journal) {
      flags |= V6FS::V6_MKLOG;

      // When you have completed the project, you can uncomment
      // the following line:
      //
      // flags |= V6FS::V6_REPLAY;
    }
    try {
      fs = new V6FS(image, cache, flags);
    } catch (const std::exception &e) {
      fprintf(stderr, "Error: %s\n", e.what());
      exit(1);
    }
  }
  if (options.suppress_commit && fs && fs->log_)
    fs->log_->suppress_commit_ = true;

  // Spawn a process with the reading end of a pipe, so as to detect
  // the parent crashing by EOF on the pipe.  When the parent
  // crashes, attempt to clean up the fuse mount point.
  if (!options.show_help && mountpoint)
    mount_cleanup(progdir, mountpoint);

  ret = fuse_main(args.argc, args.argv, &v6_oper, nullptr);
  fuse_opt_free_args(&args);
  delete fs;
  return ret;
}
