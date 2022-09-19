#pragma once

/** ilist.h \brief Simple intrusive circular linked list for plain old
 * data structures.
 *
 * The benefit of intrusive lists is that they require no memory
 * allocation and that elements are their own iterators.  Hence,
 * methods such as remove, next, and prev are static, and you can
 * remove an element from whatever list it's on without even knowing
 * the list.  This implementation also lets you check whether an
 * element is on any list by calling the is_linked() method.
 *
 * Every object placed in an \c ilist must contain an \c ilist_entry.
 * An object can be placed in multiple lists if it has multiple fields
 * of type \c ilist_entry.  The particular \c ilist_entry to use is
 * part of the type of the \c ilist.
 *
 * The implementation requires that the \c ilist_entry be at the same
 * offset in every object on a list.  C++ technically only guarantees
 * this for plain old data structures, but in practice it will work
 * for anything \a except cases where the \c ilist_entry is in a
 * virtual base class.
 *
 * If you delete an object that is on a list, the \c ilist_entry
 * destructor automatically removes the item from the list.
 *
 * On the other hand, you MUST NOT delete an \c ilist that still
 * contains elements.  The methods \c remove_all and \c delete_all may
 * be useful if you need to destroy an \c ilist that could still
 * contain elements.  Keep in mind that global ilists are destroyed on
 * program exit, and hence will cause a crash if non-empty.
 *
 * A simple example:
 *
 * \code
 * struct foo {
 *   int n;
 *   ilist_entry link;
 *   foo(int nn) : n (nn) {}
 * };
 * using foolist = ilist<&foo::link>;
 *
 * int
 * main()
 * {
 *   foolist fl;
 *   foo *fp = new foo(0);
 *   fl.push_back(fp);
 *   foolist::remove(fp);
 * }
 * \endcode
 */

#include "imisc.hh"

#ifndef ILIST_CHECK
#define ILIST_CHECK 1
#endif

#if ILIST_CHECK
#define ILIST_ASSERT assert
#else // !ILIST_CHECK
#define ILIST_ASSERT(x)
#endif // !ILIST_CHECK

namespace detail {
struct ilist_node {
  // Set up as a circular list, where the extra bit means you are
  // pointing to the head instead of to an ordinary element.  When
  // prev_ is 0, it indicates the node is not on any list.
  ptr_and_bit<ilist_node> next_;
  ptr_and_bit<ilist_node> prev_;
};
} // namespace detail

//! Every structure in an \c ilist must contain an \c ilist_entry.
struct ilist_entry : private detail::ilist_node {
  ilist_entry() = default;
  ilist_entry(ilist_entry &&v) {
    if (v.prev_.both_) {
      *static_cast<detail::ilist_node *>(this) = v;
      v.prev_.both_ = 0;
      next_->prev_.set(this);
      prev_->next_.set(this);
    }
  }
  //! On destruction, the item is removed from a list.
  ~ilist_entry() {
    if (prev_.both_)
      unlink();
  }

  //! Returns \c true if the object is in a list.
  bool is_linked() const { return prev_.both_; }
  //! Removes the object from the list it is in.  The behavior is
  //! undefined in the object is not in a list.
  void unlink() {
    ILIST_ASSERT(is_linked());
    prev_->next_ = next_;
    next_->prev_ = prev_;
    prev_.both_ = 0;
  }
  template <auto Link, typename T> friend class ilist;
};

//! Intrusive list.  The argument \c Link must be a pointer-to-member
//! of type \c ilist_entry.
template <auto Link, typename V = detail::struct_of_field<Link>> class ilist {
public:
  using value_type = V;
  static constexpr ilist_entry value_type::*entry_ptr = Link;

private:
  using ptr_type = detail::ptr_and_bit<detail::ilist_node>;
  detail::ilist_node head_ = {{&head_, true}, {&head_, true}};

  static value_type *to_value(ptr_type n) {
    return n.bit() ? nullptr
                   : static_cast<value_type *>(struct_from_field(
                         entry_ptr, static_cast<ilist_entry *>(n.ptr())));
  }
  void reinit() { head_.next_ = head_.prev_ = ptr_type{&head_, true}; }

public:
  ilist() = default;
  ilist(const ilist &) = delete;
  ~ilist() { assert(empty()); }
  ilist &operator=(const ilist &) = delete;
  bool empty() const { return head_.next_.bit(); }

  void push_front(value_type *vv) {
    detail::ilist_node *v = &(vv->*entry_ptr);
    ILIST_ASSERT(!v->prev_.both_);
    v->next_ = head_.next_;
    v->prev_.set(&head_, true);
    head_.next_.set(v);
    v->next_->prev_.set(v);
  }
  void push_back(value_type *vv) {
    detail::ilist_node *v = &(vv->*entry_ptr);
    ILIST_ASSERT(!v->prev_.both_);
    v->prev_ = head_.prev_;
    v->next_.set(&head_, true);
    head_.prev_.set(v);
    v->prev_->next_.set(v);
  }
  //! Insert an element before another element.  \arg pos is the
  //! element before which a new element must be inserted.  It must
  //! already be in the list, but can be \c nullptr to insert at the
  //! end of the list.  \arg to_insert is an element that must not
  //! already be in a list.
  void insert(value_type *pos, value_type *to_insert) {
    detail::ilist_node *vnew = &(to_insert->*entry_ptr);
    ILIST_ASSERT(!vnew->prev_.both_); // to_insert must not be in list
    detail::ilist_node *vpos = pos ? &(pos->*entry_ptr) : &head_;
    ILIST_ASSERT(vpos->prev_.both_); // pos must be in list
    vnew->prev_ = vpos->prev_;
    vpos->prev_.set(vnew);
    vnew->next_ = vnew->prev_->next_;
    vnew->prev_->next_.set(vnew);
  }
  static void remove(value_type *v) { (v->*entry_ptr).unlink(); }

  value_type *front() { return to_value(head_.next_); }
  const value_type *front() const { return to_value(head_.next_); }
  value_type *pop_front() {
    value_type *v = front();
    if (v)
      remove(v);
    return v;
  }

  value_type *back() { return to_value(head_.prev_); }
  const value_type *back() const { return to_value(head_.prev_); }
  value_type *pop_back() {
    value_type *v = back();
    if (v)
      remove(v);
    return v;
  }

  static value_type *next(value_type *v) {
    return to_value((v->*entry_ptr).next_);
  }
  static const value_type *next(const value_type *v) {
    return to_value((v->*entry_ptr).next_);
  }
  static value_type *prev(value_type *v) {
    return to_value((v->*entry_ptr).prev_);
  }
  static const value_type *prev(const value_type *v) {
    return to_value((v->*entry_ptr).prev_);
  }

  //! Remove every element on the list.
  void remove_all() {
    for (ptr_type n = head_.next_; !n.bit(); n = n->next_)
      n->prev_.both_ = 0;
    reinit();
  }
  //! Remove and delete every element on the list.
  void delete_all() {
    ptr_type n, nn;
    for (n = head_.next_; !n.bit(); n = nn) {
      nn = n->next_;
      delete to_value(n);
    }
    reinit();
  }

  using iterator = detail::fake_iterator<value_type, ilist>;
  using const_iterator = detail::fake_iterator<const value_type, ilist>;
  iterator begin() { return front(); }
  const_iterator begin() const { return front(); }
  iterator end() { return nullptr; }
  const_iterator end() const { return nullptr; }

#if ILIST_CHECK
  void check() {
    if (head_.next_.bit()) {
      ILIST_ASSERT(head_.prev_.bit());
      ILIST_ASSERT(head_.next_.ptr() == &head_);
      ILIST_ASSERT(head_.prev_.ptr() == &head_);
      return;
    }
    ILIST_ASSERT(head_.next_.ptr() != &head_);
    ILIST_ASSERT(head_.prev_.ptr() != &head_);
    ILIST_ASSERT(head_.next_.ptr()->prev_.bit());
    ILIST_ASSERT(head_.next_.ptr()->prev_.ptr() == &head_);
    ILIST_ASSERT(head_.prev_.ptr()->next_.bit());
    ILIST_ASSERT(head_.prev_.ptr()->next_.ptr() == &head_);
  }
#endif // ILIST_CHECK
};
