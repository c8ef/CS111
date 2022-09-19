#pragma once

/** \file imisc.h
 * \brief Miscellaneous building blocks useful for intrusive containers.
 */

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace detail {

//! Return the offset of a particular structure member in a plain old
//! datatype structure.  Will fail if the member is in a virtual base
//! class or otherwise not at a constant offset.
template <typename S, typename F> inline std::ptrdiff_t field_offset(F S::*fp) {
  // Warning: this code technically has undefined behavior (because
  // it dereferences a NULL pointer), but works with today's
  // compilers.  A deficiency in the design of offsetof makes it
  // difficult to implement intrusive data structures without a hack
  // such as this.  See this C++ proposal for a good discussion:
  // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0908r0.html
  static constexpr const S *z = 0;
  return reinterpret_cast<ptrdiff_t>(std::addressof(z->*fp));
}

//! Return a pointer to the structure containing a field from a
//! pointer to the field.  Uses \c field_offset, and consequently
//! requires the field always to have the same offset (i.e., not be in
//! a virtual base class).
template <typename S, typename F> inline S *struct_from_field(F S::*fp, F *f) {
  return reinterpret_cast<S *>(reinterpret_cast<char *>(f) - field_offset(fp));
}

template <typename S, typename F>
inline const S *struct_from_field(F S::*fp, const F *f) {
  return reinterpret_cast<const S *>(reinterpret_cast<const char *>(f) -
                                     field_offset(fp));
}

template <auto Field> struct struct_of_field_helper;
template <typename S, typename T, T S::*Field>
struct struct_of_field_helper<Field> {
  using type = S;
};

// Given struct S { T Field; };
// struct_of_field<&S::Field> is type S.
template <auto Field>
using struct_of_field = typename struct_of_field_helper<Field>::type;

//! Store a pointer and a bit together.  It must be the case that your
//! pointers are aligned to even addresses.  \tparam T is the type of
//! the pointer.
template <typename T> struct ptr_and_bit;
template <typename T> bool operator==(ptr_and_bit<T> a, ptr_and_bit<T> b);
template <typename T> bool operator!=(ptr_and_bit<T> a, ptr_and_bit<T> b);
template <typename T> void swap(ptr_and_bit<T> &a, ptr_and_bit<T> &b);
template <typename T> struct ptr_and_bit {
  using ptr_type = T *;

private:
  static constexpr std::intptr_t from_ptr(ptr_type p) {
    static_assert(alignof(T) >= 2);
    return reinterpret_cast<std::intptr_t>(p);
  }

public:
  //! Single field encoding both the pointer and the bit.  The least
  //! significant bit of \c both_ is the bit, while the remaining
  //! bits are the pointer (which obviously must have alignment at
  //! least 2, enforced by the static assert in \c from_ptr).
  std::intptr_t both_;

  //! Initialize with \c nullptr and \c false.
  constexpr ptr_and_bit() : both_{0} {}
  constexpr ptr_and_bit(const ptr_and_bit &pab) = default;
  //! Initialize with specific pointer and \c false bit.
  ptr_and_bit(ptr_type p) : both_{from_ptr(p)} {}
  //! Initialize with specific pointer and bit.
  ptr_and_bit(ptr_type p, bool b) : both_(from_ptr(p) | b) {}

  ptr_and_bit &operator=(const ptr_and_bit &pab) = default;

  //! Set the pointer to some value and clear the bit.
  void set(ptr_type p) { both_ = from_ptr(p); }
  //! Set both the pointer and the bit.
  void set(ptr_type p, bool bit) { both_ = from_ptr(p) | bit; }

  //! Set the pointer to a specific value, preserving the bit.
  void ptr(ptr_type ptr) { set(ptr, bit()); }
  //! Return the pointer value.
  ptr_type ptr() const { return reinterpret_cast<ptr_type>(both_ & ~1); }
  //! Return the pointer value.
  ptr_type operator->() const { return ptr(); }
  //! Dereference the pointer value.
  T &operator*() const { return *ptr(); }

  //! Return the bit.
  bool bit() const { return both_ & 1; }
  //! Set the bit without changing the pointer.
  void bit(bool b) { both_ = (both_ & ~1) | b; }

  friend bool operator==(ptr_and_bit a, ptr_and_bit b) {
    return a.both_ == b.both_;
  }
  friend bool operator!=(ptr_and_bit a, ptr_and_bit b) {
    return a.both_ != b.both_;
  }
  friend void swap(ptr_and_bit &a, ptr_and_bit &b) {
    std::swap(a.both_, b.both_);
  }
};

//! Fake iterator type, used to make intrusive containers support
//! range for syntax.  It's fake because you can't walk the end
//! iterator backwards.
template <typename V, typename T> struct fake_iterator {
  V *v_;
  constexpr fake_iterator(V *v) : v_(v) {}
  fake_iterator(const fake_iterator<const V, T> &fi) : v_(fi.v_) {}
  V *operator->() const { return v_; }
  V &operator*() const { return *v_; }
  friend bool operator==(fake_iterator a, fake_iterator b) {
    return a.v_ == b.v_;
  }
  friend bool operator!=(fake_iterator a, fake_iterator b) {
    return a.v_ != b.v_;
  }
  fake_iterator &operator++() {
    v_ = T::next(v_);
    return *this;
  }
  V *operator++(int) {
    V *v{v_};
    ++*this;
    return v;
  }
  fake_iterator &operator--() {
    v_ = T::prev(v_);
    return *this;
  }
  V *operator--(int) {
    V *v{v_};
    --*this;
    return v;
  }
};

} // namespace detail
