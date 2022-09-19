#include <iostream>
#include <set>

#include "cache.hh"
#include "v6fs.hh"

void report(const char *msg, const std::exception *e) {
  const char *emsg = "unknown exception";
  if (e)
    emsg = e->what();
  if (msg)
    fprintf(stderr, "%s: %s\n", msg, emsg);
  else
    fprintf(stderr, "%s\n", emsg);
}

bool CacheEntryBase::can_evict() {
  if (refcount_ > 0)
    return false;
  if (!logged_)
    return true;
  return V6Log::le(lsn_, fs().log_->committed_);
}

CacheEntryBase *CacheBase::lookup(V6FS *dev, uint16_t id) {
  CacheEntryBase *e = index_[{dev, id}];
  if (e)
    return touch(e);
  e = alloc();
  if (!e) {
    flush_all_logs();
    e = alloc();
  }
  if (!e) {
    std::cout << oom_ << std::endl;
    throw resource_exhausted(oom_.c_str(), -ENOMEM);
  }
  e->dev_ = dev;
  e->id_ = id;
  index_.insert(e);
  return touch(e);
}

void CacheBase::free_entry(CacheEntryBase *e) {
  // If the next line throws an assertion failure, you attempted to
  // double-free a cache entry.
  e->idxlink_.unlink();
  e->logged_ = e->dirty_ = e->initialized_ = false;
  e->dev_ = nullptr;
  e->id_ = 0;
  lrulist_.remove(e);
  lrulist_.push_front(e);
}

void CacheBase::free(V6FS *dev, uint16_t id) {
  if (CacheEntryBase *e = index_[{dev, id}])
    free_entry(e);
}

bool CacheBase::flush_all() noexcept {
  return flush_range(index_.min(), nullptr);
}

bool CacheBase::flush_dev(V6FS *dev) noexcept {
  return flush_range(index_.lower_bound(CacheEntryBase::CacheKey{dev, 0}),
                     index_.lower_bound(CacheEntryBase::CacheKey{dev + 1, 0}));
}

void CacheBase::invalidate_dev(V6FS *dev) noexcept {
  CacheEntryBase *b = index_.lower_bound(CacheEntryBase::CacheKey{dev, 0}),
                 *end =
                     index_.lower_bound(CacheEntryBase::CacheKey{dev + 1, 0});
  while (b != end) {
    CacheEntryBase *c = b;
    b = index_.next(b);
    free_entry(c);
  }
}

// Move entry to the back of the LRU list when it is used
CacheEntryBase *CacheBase::touch(CacheEntryBase *e) {
  lrulist_.remove(e);
  lrulist_.push_back(e);
  return e;
}

// Find a not recently used entry that is free or can be evicted.
CacheEntryBase *CacheBase::alloc() {
  for (CacheEntryBase *e = lrulist_.front(); e; e = lrulist_.next(e))
    if (!e->idxlink_.is_linked()) {
      return e;
    } else if (e->can_evict()) {
      if (e->dirty_) {
        e->writeback();
        e->dirty_ = e->logged_ = false;
      }
      e->idxlink_.unlink();
      e->logged_ = e->dirty_ = e->initialized_ = false;
      return e;
    }
  return nullptr;
}

bool CacheBase::can_alloc(int want) {
  int n = want;
  for (CacheEntryBase *e = lrulist_.front(); e && n > 0; e = lrulist_.next(e))
    if (!e->idxlink_.is_linked() || e->can_evict())
      --n;
  if (!n)
    return true;
  flush_all_logs();
  n = want;
  for (CacheEntryBase *e = lrulist_.front(); e && n > 0; e = lrulist_.next(e))
    if (!e->idxlink_.is_linked() || e->can_evict())
      --n;
  return !n;
}

// If we can't evict any cache slots, it's probably because we've
// gotten too far ahead of the log and are unable to write back
// entries that have not been stably logged yet.  This funciton
// attempts to force all logs.
void CacheBase::flush_all_logs() {
  std::set<V6FS *> fses;
  for (CacheEntryBase *ce = lrulist_.front(); ce; ce = lrulist_.next(ce))
    if (ce->dev_->log_)
      fses.insert(ce->dev_);
  for (V6FS *dev : fses)
    dev->log_->flush();
}

bool CacheBase::flush_range(CacheEntryBase *b, CacheEntryBase *end) noexcept {
  bool ok = true;
  while (b != end) {
    CacheEntryBase *c = b;
    b = index_.next(b);
    if (c->dirty_ &&
        (!c->logged_ || V6Log::le(c->lsn_, c->dev_->log_->committed_)))
      try {
        c->writeback();
        c->dirty_ = c->logged_ = false;
      } catch (std::exception &e) {
        ok = false;
        report("Cache flush", &e);
      }
  }
  return ok;
}
