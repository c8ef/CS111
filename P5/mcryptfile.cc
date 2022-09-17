#include <cstring>
#include <iostream>
#include <unordered_map>

#include <sys/stat.h>

#include "mcryptfile.hh"
#include "vm.hh"

MCryptFile::MCryptFile(Key key, std::string path) : CryptFile(key, path) {
  // You need to implement this
}

MCryptFile::~MCryptFile() {
  // You need to implement this
}

char *MCryptFile::map(size_t min_size) {
  // You need to implement this
  return nullptr;
}

void MCryptFile::unmap() {
  // You need to implement this
}

void MCryptFile::flush() {
  // You need to implement this
}

void MCryptFile::set_memory_size(std::size_t npages) {
  // You need to implement this
}
