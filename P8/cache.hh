#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "ilist.hh"
#include "itree.hh"
#include "util.hh"

struct V6FS;

// Report an error on stderr
void report(const char *msg, const std::exception *e);

struct resource_exhausted : std::runtime_error {
  int error;
  resource_exhausted(const char *msg, int err)
      : runtime_error(msg), error(err) {}
};

struct CacheEntryBase {
  V6FS *dev_;
  uint16_t id_; // Identifier for cache (block# or inum)
  int refcount_ = 0;
  bool initialized_ = false;
  bool dirty_ = false;
  bool logged_ = false; // Contains a logged patch
  uint32_t lsn_;        // Log sequence number if logged_
  ilist_entry lrulink_;
  itree_entry idxlink_;

  V6FS &fs() const { return *dev_; }
  bool can_evict();
  void mark_dirty() { dirty_ = true; }
  virtual void writeback() = 0;

  using CacheKey = std::pair<V6FS *, uint16_t>;
  CacheKey cache_key() const { return {dev_, id_}; }
};

class CacheBase {
public:
  // Remove an item from the index, discard its contents, and put it
  // on the front of the LRU list for the next allocation.
  void free_entry(CacheEntryBase *e);

  // Free an entry with id (if cached) without writing it back.
  void free(V6FS *dev, uint16_t id);

  // Write back all dirty entries.
  bool flush_all() noexcept;
  bool flush_dev(V6FS *dev) noexcept;

  // Free all entries associated with dev (not writing them back).
  void invalidate_dev(V6FS *dev) noexcept;

  // The next n allocations will succeed.
  bool can_alloc(int n = 1);

protected:
  std::string oom_ = "cache full";
  ilist<&CacheEntryBase::lrulink_> lrulist_;
  itree<&CacheEntryBase::cache_key, &CacheEntryBase::idxlink_> index_;

  CacheBase() = default;
  CacheEntryBase *lookup(V6FS *dev, uint16_t id);
  CacheEntryBase *try_lookup(V6FS *dev, uint16_t id) {
    return index_[{dev, id}];
  }

private:
  CacheEntryBase *touch(CacheEntryBase *);
  CacheEntryBase *alloc();
  void flush_all_logs();
  bool flush_range(CacheEntryBase *begin, CacheEntryBase *end) noexcept;
};

// A reference to a cached object.  References can be copied.  When
// the number of outstanding Refs to an object goes to zero, the
// object becomes elligible for eviction.
template <typename T> class Ref {
  T *p_ = nullptr;
  static void inc(CacheEntryBase *p) {
    if (p)
      ++p->refcount_;
  }
  static void dec(CacheEntryBase *p) {
    if (p)
      --p->refcount_;
  }

public:
  using element_type = T;

  Ref() = default;
  Ref(std::nullptr_t) {}
  Ref(T *p) : p_(p) { inc(p_); }
  Ref(Ref &&r) : p_(r.p_) { r.p_ = nullptr; }
  Ref(const Ref &r) : Ref(r.get()) {}
  ~Ref() { dec(p_); }

  Ref &operator=(Ref &&r) {
    std::swap(p_, r.p_);
    return *this;
  }
  Ref &operator=(const Ref &r) {
    T *oldp = p_;
    p_ = r.p_; // assign before dec() for exception safety
    inc(p_);
    dec(oldp);
    return *this;
  }
  Ref &operator=(std::nullptr_t) {
    T *oldp = p_;
    p_ = nullptr;
    dec(oldp);
    return *this;
  }

  T *get() const { return p_; }
  T *operator->() const { return p_; }
  T &operator*() const { return *p_; }
  explicit operator bool() const { return p_; }
};

template <typename T> struct Cache : CacheBase {
  using value_type = T;

  std::unique_ptr<value_type[]> entries_;
  const size_t size_;

  explicit Cache(size_t size)
      : CacheBase(), entries_(new value_type[size]), size_(size) {
    for (size_t i = 0; i < size; ++i)
      lrulist_.push_back(&entries_[i]);
    oom_ = std::string(typeid(T).name()) + " cache full";
  }
  ~Cache() { flush_all(); }

  // Look up item in cache
  Ref<value_type> operator()(V6FS *dev, uint16_t id) {
    if (CacheEntryBase *ce = lookup(dev, id))
      return static_cast<value_type *>(ce);
    return nullptr;
  }

  Ref<value_type> try_lookup(V6FS *dev, uint16_t id) {
    return static_cast<value_type *>(CacheBase::try_lookup(dev, id));
  }

  // Remove an item from the index, discarding its contents, and put
  // it on the front of the LRU list so it will be preferentially
  // recycled.
  using CacheBase::free;
  void free(value_type *e) { free_entry(e); }
  void free(const Ref<value_type> &e) { free_entry(e.get()); }

  bool contains(void *p) const {
    return &entries_[0] <= p && p < &entries_[size_];
  }
  value_type *entry_containing(void *p) {
    ptrdiff_t bytes =
        reinterpret_cast<char *>(p) - reinterpret_cast<char *>(entries_.get());
    ptrdiff_t n = bytes / sizeof(value_type);
    if (bytes < 0 || size_t(n) >= size_)
      throw std::out_of_range("Cache::entry_containing: bad pointer");
    return &entries_[n];
  }
};
