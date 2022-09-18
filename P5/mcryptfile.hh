#pragma once

#include <map>
#include <optional>

#include "cryptfile.hh"

// An MCryptFile is a CryptFile that supports one additional feature.
// In addition to the base functionality of reading and writing data,
// you can also memory-map the file--just like the mmap system call,
// except that pages are decrypted on the way in and encrypted when
// written back out.
struct MCryptFile : public CryptFile {
  // Opens file path using encryption key key.  Throws a
  // std::system_error if the file cannot be opened.
  MCryptFile(Key key, std::string path);
  ~MCryptFile();

  // Create a region that memory-maps the decrypted contents of the
  // file and return the address of the first byte of the region.  If you
  // want to grow the file, you can supply a min_size > 0, and the
  // mapped region will be the larger of min_size and the file's actual
  // size.  If you want to grow a file after it has already been mapped,
  // unmap() and then re-map() it, which will likely move map_base() and
  // invalidate any old pointers into the previous mapped region.
  char *map(std::size_t min_size = 0);

  // Remove the mapping created by map, invalidating all pointers.
  void unmap();

  // Address of the first byte of the memory mapped file.  It is an
  // error to call this before calling map() or after calling unmap().
  char *map_base() { return map_base_; }

  // Size of mapped file (once map() has been called)
  std::size_t map_size() { return map_size_; }

  // Flush all changes back to the encrypted file; pages currently
  // in memory remain there.
  void flush();

  // Specifies size of the physical memory pool shared by all
  // MCryptFile objects. Must be invoked before any MCryptFile
  // objects have been created; later indications will have no effect.
  static void set_memory_size(std::size_t npages);

  void fault(char *va);

private:
  static PhysMem *phy_mem_;
  static size_t page_num_;
  static size_t vm_instance_;

  VMRegion *vir_mem_;
  size_t map_size_;
  VPage map_base_;

  std::map<VPage, int> page_env_;
  std::map<VPage, PPage> v2p_;
};
