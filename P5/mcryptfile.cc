#include <algorithm>
#include <cstring>
#include <iostream>
#include <unordered_map>

#include <sys/stat.h>

#include "mcryptfile.hh"
#include "vm.hh"

PhysMem *MCryptFile::phy_mem_ = nullptr;
size_t MCryptFile::page_num_ = 1000;
size_t MCryptFile::vm_instance_ = 0;

MCryptFile::MCryptFile(Key key, std::string path)
    : CryptFile(key, path), vir_mem_(nullptr), map_size_(0), map_base_(0),
      page_env_(), v2p_() {}

MCryptFile::~MCryptFile() {
  flush();
  for (const auto pair : v2p_) {
    vir_mem_->unmap(pair.first);
    phy_mem_->page_free(pair.second);
  }
  v2p_.clear();
  page_env_.clear();
  map_size_ = 0;
  map_base_ = nullptr;

  delete vir_mem_;
  vir_mem_ = nullptr;
  vm_instance_--;

  if (vm_instance_ == 0) {
    delete phy_mem_;
    phy_mem_ = nullptr;
  }
}

char *MCryptFile::map(size_t min_size) {
  min_size = std::max(min_size, file_size());
  if (!vir_mem_) {
    vir_mem_ = new VMRegion(min_size, [this](char *va) { fault(va); });
    vm_instance_++;
  }
  if (!phy_mem_) {
    phy_mem_ = new PhysMem(page_num_);
  }
  map_size_ = min_size;
  map_base_ = vir_mem_->get_base();
  return map_base_;
}

void MCryptFile::unmap() {
  flush();
  for (const auto pair : v2p_) {
    vir_mem_->unmap(pair.first);
    phy_mem_->page_free(pair.second);
  }
  v2p_.clear();
  page_env_.clear();
  map_size_ = 0;
  map_base_ = nullptr;

  delete vir_mem_;
  vir_mem_ = nullptr;
  vm_instance_--;
}

void MCryptFile::flush() {
  for (const auto pair : page_env_) {
    if (pair.second == 1) {
      aligned_pwrite(v2p_[pair.first], page_size, pair.first - map_base_);
    }
  }
}

void MCryptFile::set_memory_size(std::size_t npages) { page_num_ = npages; }

void MCryptFile::fault(char *va) {
  VPage vp = va - std::uintptr_t(va) % page_size;
  if (page_env_.count(vp) == 0) {
    PPage pp = phy_mem_->page_alloc();
    char tmp_buf[page_size + 16]{};
    aligned_pread(tmp_buf, page_size, vp - map_base_);
    memcpy(pp, tmp_buf, page_size);

    vir_mem_->map(vp, pp, PROT_READ);
    page_env_[vp] = 0;
    v2p_[vp] = pp;
  } else {
    vir_mem_->unmap(vp);
    PPage pp = v2p_[vp];
    vir_mem_->map(vp, pp, PROT_READ | PROT_WRITE);
    page_env_[vp] = 1;
  }
}