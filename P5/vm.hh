#pragma once

#include <cassert>
#include <cerrno>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>

#include <signal.h>
#include <sys/mman.h>

#include "util.hh"

// Get the size of a page on this machine (the minimum granularity of
// virtual-to-physical memory mapping).
inline std::size_t get_page_size() {
  static const std::size_t nbytes = sysconf(_SC_PAGESIZE);
  return nbytes;
};

// The page size on this machine.  Note that this is initialized at
// the start of the program; since C++ makes no guarantees about the
// order of initialization of static/global variables across modules,
// any constructors that might run during static initialization must
// use get_page_size() instead of page_size.
extern const std::size_t page_size;

// Pointer to a pseudo-physical page.  You can access memory
// read/write at a pseudo-physical page in low-level code, but the
// address will have no relation to how the memory is used in the
// application.  Hence, you should avoid exposing PPage addresses to
// applications.
using PPage = char *;

// Pointer to a page virtual address.  A valid virtual address may or
// may not be backed by a physical page, and may be invalid,
// read-only, or read-write.  VPage and PPage reference disjoint areas
// of the process address space, at most one of which is valid in any
// given context.  Though both are aliases for char *, the aliases
// make the expected area explicit in function prototypes.
using VPage = char *;

// Protection bits for pages.  These values are specified using macros
// in <sys/mman.h> and documented in the system man page for mmap(2).
// Reasonable values are:
//
//    PROT_NONE - neither loads nor stores permited to page
//    PROT_READ - loads permitted but not stores
//    PROT_READ|PROT_WRITE - (bitwise or of two values) loads+stores permitted
//
using Prot = int;

class PhysMem;

// A region of virtual memory.  Until you explicitly map physical
// pages there, a VMRegion has no memory and will generate page faults
// if you access any of its pages.  However, the virtual address range
// of a VMRegion is guaranteed to be unique and not overlap with other
// VMRegion or any other virtual memory in the process's address
// space.
class VMRegion {
public:
  // Allocate a region of virtual memory of size bytes.  Call
  // handler with the address of any page faults within the region.
  // Nbytes doesn't need to be a multiple of page_size, but if it
  // isn't, the portion of the last virtual page above size will not
  // trigger page faults.
  VMRegion(std::size_t nbytes, std::function<void(char *)> handler);

  // Release a region of virtual memory.  It is an error to free a
  // region that still has mapped pages.
  ~VMRegion();

  // Return the address of the first page in the region.
  VPage get_base() { return base_; }

  // Return the total number of bytes in the region.
  std::size_t get_size() { return nbytes_; }

  // Set the mapping for a particular VPage inside a VMRegion.  If a
  // different page was previously mapped at VPage, the old mapping
  // is discarded.  Otherwise, updates the protection bits.  If pa
  // is nullptr (in which case prot must be PROT_NONE), then the
  // mapping is removed.
  static void map(VPage va, PPage pa, Prot prot);

  // Unmap a VPage.
  static void unmap(VPage va);

private:
  // Data structure representing how a particular virtual is mapped.
  // Represents approximately the information that would be
  // contained in a page table entry.
  struct PageInfo {
    PPage pa;  // PPage backing a virtual page, or nullptr if none
    Prot prot; // Protection mode of the virtual page
    bool operator==(const PageInfo &other) const {
      return pa == other.pa && prot == other.prot;
    }
    bool operator!=(const PageInfo &other) const {
      return pa != other.pa || prot != other.prot;
    }
  };

  VPage base_;
  std::size_t nbytes_;
  const std::function<void(char *)> handler_;

  // Number of pages currently mapped in this region.
  int pages_mapped_;

  // All regions, indexed by base virtual address
  static std::map<VPage, VMRegion *> regions_;

  // Data structure recording each VPage mapped to a PPage
  struct Mapping {
    const VPage va_; // mapped here with pi_.prot protection
    PageInfo pi_;

    Mapping(VPage va);
    ~Mapping();
  };

  // All page mappings in all regions, indexed by virtual page address
  static std::unordered_map<VPage, Mapping *> pagemap_;

  // Returns the region containing a given virtual address, or nullptr
  // if none.
  static VMRegion *find(char *addr);

  // Update a mapping if anything has changed
  static void update(Mapping *m, PageInfo pi);

  // Signal handler for SIGSEGV (which gets called on page faults)
  static void fault_handler(int sig, siginfo_t *info, void *ctx);

  // Get refcount of a physical page
  static int *refcount(PPage pa);

  friend class PhysMem;
};

// PhysMem holds a fixed number of pseudo-physical pages that can be
// mapped at arbitrary addresses in VMRegion.  Each pseudo-physical
// page is backed by a real page of physical memory (modulo the
// availability of the mlock system call) and can be accessed read or
// write at its PPage pseudo-physical address.
class PhysMem {
public:
  PhysMem(std::size_t npages);
  ~PhysMem();

  std::size_t npages() { return npages_; } // Total number of pages
  std::size_t nfree() { return nfree_; }   // Number of free pages
  PPage page_alloc(); // Allocate a page, or return nullptr if out of pages.
  void page_free(PPage p); // Free an allocated page (must not be mapped)

  // PPages managed by this object have contiguous addresses; this
  // returns the address of the first (lowest) page.
  PPage pool_base() { return base_; }

private:
  const std::size_t npages_; // Total number of pages in pool
  const std::size_t size_;   // Size of memory pool in bytes
  const unique_fd fd_;       // Temporary file containing pages
  const PPage base_;         // Address of first byte of pseudo-physical
                             // memory in the pool
  std::size_t nfree_;        // Number of pages not currently allocated

  // All PhysMem objects, indexed by pool_.  Use a function so we
  // can create global PhysMem objects without worrying about the
  // order of initialization.
  static std::map<PPage, PhysMem *> &pools() {
    // The code below ensures that pools is never destroyed;
    // this is necessary to prevent assertion failures occurring
    // because exit-time destruction could occur in a bad order.
    static std::map<PPage, PhysMem *> &val = *new std::map<PPage, PhysMem *>;
    return val;
  }

  static PhysMem *find(PPage p) {
    PhysMem *pm;
    assert(std::uintptr_t(p) % page_size == 0);
    std::map<PPage, PhysMem *>::iterator it = pools().upper_bound(p);
    // If any of these assertions fails, you tried to use a PPage
    // that was not in any PhysMem object.
    assert(it != pools().begin());
    if (it != pools().end()) {
      it--;
      pm = it->second;
    } else {
      assert(pools().size() != 0);
      pm = pools().rbegin()->second;
    }
    assert(p >= pm->base_ && p < pm->base_ + pm->size_);
    return pm;
  }

  // We keep free pages in a singly linked list.  To catch some
  // egregious use-after-free bugs, we sandwich the next pointer
  // between two randomly generated constants and periodically check
  // that these constants have not been overwritten.
  struct FreePage {
    // These are just random constants for detecting corruption.
    static constexpr uint64_t MAGIC1 = 0xb587a9ce779288b5;
    static constexpr uint64_t MAGIC2 = 0xaa75b1b8ac4cd7d0;
    static constexpr uint64_t GARBAGE = 0x702e0f91a2a6bec7;

    volatile uint64_t magic1_;
    FreePage *next_;
    volatile uint64_t magic2_;

    FreePage() : magic1_(MAGIC1), magic2_(MAGIC2) {}
    ~FreePage() {
      check();
      magic1_ = magic2_ = GARBAGE;
    }

    void check() { assert(magic1_ == MAGIC1 && magic2_ == MAGIC2); }
    static FreePage *construct(PPage addr) {
      assert(std::uintptr_t(addr) % get_page_size() == 0);
      return new (static_cast<void *>(addr)) FreePage;
    }
    PPage destroy() {
      PPage ret = reinterpret_cast<PPage>(this);
      this->~FreePage();
      return ret;
    }
  };
  FreePage *free_pages_;

  std::vector<int> refcounts_;
  int *refcount(PPage p) {
    // If this assertion fails, you tried to use a PPage that was
    // not allocated by this PhysMem object.
    assert(base_ <= p && p < base_ + size_);
    return &refcounts_[(p - base_) / page_size];
  }

  friend class VMRegion;
};
