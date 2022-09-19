#pragma once

/** \file itree.h
 * \brief Intrusive red-black tree.
 *
 * Intrusive trees let you index plain old data structures that
 * contain a field of type \c itree_entry.  If a structure contains
 * multiple \c itree_entry fields, it can be placed in multiple trees.
 * Because an \c itree_entry already contains space for all necessary
 * pointers, this library never allocates memory.  Moreover, there is
 * no need for separate iterators---you efficiently navigate or remove
 * an object from a tree using only a pointer to the object.  (But
 * note removing an object is not the same as erasing it---the object
 * is not deleted, just de-indexed from the tree.)
 *
 * This header exposes two types.  \c itree is a simple itree for
 * structures in which a single field serves as the key.  \c
 * itree_core allows greater flexibility in the comparison and lookup
 * functions.  An example:
 *
 * \code
 * struct foo {
 *   int n;
 *   itree_entry link;
 *   foo(int nn) : n (nn) {}
 * };
 *
 * // template args: key field, entry_entry field
 * using footree = itree<&foo::n, &foo::link>;
 *
 * void
 * example()
 * {
 *   footree t;
 *
 *   // Insert 10 items into tree
 *   for (int i = 0; i < 10; i++)
 *     t.insert(new foo{i});
 *
 *   // Duplicate keys are okay--tree contains two items of each value
 *   for (int i = 0; i < 10; i++)
 *     t.insert(new foo{i});
 *
 *   // Delete all even numbers
 *   for (int i = 0; i < 10; i += 2) {
 *     // Loop because we inserted two of each number
 *     for (foo *fp = t[i], *fp_next; fp && fp->n == i; fp = fp_next) {
 *       fp_next = t.next(fp);
 *       // Deleting an object in a tree automatically removes it from the tree
 *       delete fp;
 *     }
 *   }
 *
 *   // Iterate through the tree in order
 *   // Prints:  1  1  3  3  5  5  7  7  9  9
 *   for (foo *fp = t.min(); fp; fp = t.next(fp))
 *     cout << "  " << fp->n;
 *   cout << endl;
 *
 *   // It is an error to destroy a non-empty tree
 *   t.delete_all();
 * }
 * \endcode
 *
 * Some important points to know about \c itree_core and (and hence \c
 * itree):
 *
 * - It is an error to destroy an \c itree_core that contains members.
 *   This is because intrusive trees do not allocate their members, so
 *   if there are still objects in the tree the code doesn't know
 *   whether to delete them or simply remove them from the tree.  If
 *   you do still have objects in the tree, then before destryoing it
 *   you should call either the \c remove_all() or \c delete_all()
 *   method, depending on how you want this handled.
 *
 * - Most methods operate on pointers rather than references, to
 *   reflect the semantics that the tree is not copying or allocating
 *   objects.  \c nullptr is returned to indicate no object, as when a
 *   lookup fails or when iterating past the first or last item.
 *
 * - An \c itree_entry knows whether it is in an \c itree_core, and if
 *   so in which one.  It is an error to insert an object multiple
 *   times into the same or different trees using the same \c
 *   itree_entry without first removing the object.  Similarly, it is
 *   an error to remove an object from an \c itree_core other than the
 *   one into which it was inserted.  All of these errors should
 *   trigger an immediate assertion failure.
 *
 * - It is perfectly fine to delete an object that is in an \c
 *   itree_core.  The destructor for \c itree_entry will simply remove
 *   the object from the tree at this point.  Be aware that there is
 *   no locking, however.  Therefore it is unsafe to delete objects in
 *   the same tree in different threads.
 *
 * - This is not a header-only data structure.  Rather, there is one
 *   implementation for all types that does the right thing under the
 *   assumption that a single offset value can be used to get between
 *   an \c itree_entry and containing structure for every value in the
 *   same instantiation of the \c itree_core template.  By the letter
 *   of the C++ standard, this means only plain-old data structures
 *   work.  In practice, most data structures will work, but make sure
 *   the \c itree_entry is in the same data structure.  Trying to do
 *   something crazy like using an \c itree_entry in a virtual base
 *   class will fail horribly---likely by an immediate segmentation
 *   fault.
 *
 * - Many of the "methods" of \c itree_core, including \c
 *   itree_core::next() and \c itree_core::prev(), are actually static
 *   methods, which may be useful to implement functions that navigate
 *   the tree without passing around a pointer to the tree.  However,
 *   it is an error to use the functions on an object not in a tree.
 *   You can check whether an object \c v is in a tree by calling \c
 *   itree_core::container_of(&v), which will return \c nullptr if the
 *   object is not in a tree.
 *
 * - You cannot copy \c itree_entry or \c itree_core objects.
 */

#include <cassert>
#include <cstddef>

#include "imisc.hh"

namespace detail {

template <auto Field> struct type_of_field_helper;
template <typename S, typename T, T S::*Field>
struct type_of_field_helper<Field> {
  using type = T;
};

// Given struct S { T Field; };
// type_of_field<&S::Field> is type T.
template <auto Field>
using type_of_field = typename type_of_field_helper<Field>::type;

// The key can be either a field pointer, or a method that returns the
// key.  The second template argument is to work around a bug
// introduced in gcc 11.1, and should be removed once it is no longer
// needed to compile with the most recent version of gcc.
template <auto Ptr, bool = std::is_member_function_pointer_v<decltype(Ptr)>>
struct key_helper;

template <typename K, typename V, K V::*F> struct key_helper<F, false> {
  using key_type = std::remove_reference_t<K>;
  using value_type = V;
  static const key_type &key(const value_type &v) { return v.*F; }
};
template <typename K, typename V, K (V::*F)() const>
struct key_helper<F, true> {
  using key_type = std::remove_reference_t<K>;
  using value_type = V;
  static key_type key(const value_type &v) { return (v.*F)(); }
};

template <auto KeyPtr,
          typename Compare = std::less<typename key_helper<KeyPtr>::key_type>>
struct field_compare {
  using helper = key_helper<KeyPtr>;
  using key_type = typename helper::key_type;
  using value_type = typename helper::value_type;
  using compare_type = Compare;

  bool operator()(const value_type &v1, const value_type &v2) const {
    return compare_type()(helper::key(v1), helper::key(v2));
  }
  bool operator()(const key_type &k, const value_type &v) const {
    return compare_type()(k, helper::key(v));
  }
  bool operator()(const value_type &v, const key_type &k) const {
    return compare_type()(helper::key(v), k);
  }
};

} // namespace detail

//! Opaque data structure that must be included in every data
//! structure that will be indexed by an \c itree (or \c itree_core).
class itree_entry;

//! The core red-black tree structure, without information about keys.
//! \tparam Link is the pointer to an \c itree_entry field type in a
//! structure of type \c V.  There may be multiple \c itree_entries
//! and hence multiple tree types in \c V.  \tparam Compare is a
//! comparison functor type, which must have a trivial constructor and
//! <tt>bool operator()(const V&, const V&) const</tt>.  \c Compare
//! must be able to compare two \c const \c V& objects.  If it can
//! additionally compare \c V to other types in both
//! directions---i.e., <tt>C()(v, k)</tt> and <tt>C()(k,
//! v)</tt>---then more types can passed to the \c itree_core::find,
//! \c itree_core::lower_bound, and \c itree_core::upper_bound_prev
//! functions.
template <typename V, auto Link, typename Compare = std::less<V>>
class itree_core;

//! Simple intrusive map structure for plain old data structures
//! containing a key field and a field of type \c itree_entry.
//! \tparam KeyPtr is the field with the key (should the field be \c
//! const, then \c K should be const, too).  \tparam V is type type of
//! the structure that will be indexed by the \c itree.  \tparam KP is
//! the pointer to the key field in \c V.  \tparam EP is the pointer
//! to the \c itree_entry field.  \c C is an comparison function for
//! keys, which defaults to std::less<K> if unspecified.
//!
//! An example of how to instantiate this template:
//! \code
//! struct myval {
//!   int key;
//!   itree_entry link;
//! };
//!
//! using footree = itree<&foo::key, &foo::link>;
//! \endcode
//!
//! Most of the functionality is provided by the supertype \c
//! itree_core.  However, \c itree does two useful things compared to
//! \c itree_core.  First, the comparison type in \c itree_core is not
//! \c C, but rather something that knows how to compare objects of
//! type \c V both with each other and with objects of type \c K.
//! This allows you to use methods such as itree_core::find and
//! itree_core::lower_bound with arguments of type \c K.  In addition,
//! \c itree provides an itree::operator[]() function for quick
//! lookup.
template <auto KeyPtr, auto Link, typename V = detail::struct_of_field<Link>,
          typename Compare =
              typename detail::field_compare<KeyPtr>::compare_type>
class itree;

namespace detail {

enum class itree_color { RED, BLACK };

class itree_base;

//! Private internals of an \c itree_entry.
struct itree_node {
private:
  // High bits are address of base tree; low bit 0 = RED; 1 = BLACK
  ptr_and_bit<itree_base> base_;

public:
  itree_node *up;
  itree_node *left;
  itree_node *right;

  detail::itree_color color() const {
    assert(base_.both_);
    return base_.bit() ? itree_color::BLACK : itree_color::RED;
  };
  void color(detail::itree_color c) {
    assert(base_.both_);
    base_.bit(c != itree_color::RED);
  }

  bool in_tree() const { return base_.both_; }
  itree_base *base() const { return base_.ptr(); }
  // Set base and turn the node RED.
  void base(itree_base *b) { base_.set(b); }
};

//! Private internals of an \c itree_core.  No template parameters, so
//! that one set of functions can work on all types.
class itree_base {
  void left_rotate(itree_node *);
  void right_rotate(itree_node *);
  void insert_fixup(itree_node *z);
  // Make node (or root) that points to u point to v instead
  void transplant(itree_node *u, itree_node *v) {
    pointer_to(u) = v;
    if (v)
      v->up = u->up;
  }
  void delete_fixup(itree_node *x, itree_node *xp);
  void check_node(itree_node *n, unsigned bh, unsigned wanted_bh) const;

public:
  itree_node *root_;

  itree_base() : root_(nullptr){};
  itree_base(const itree_base &) = delete;
  itree_base &operator=(const itree_base &) = delete;

  virtual bool cmp(const itree_node *, const itree_node *) const = 0;
  itree_node *&pointer_to(itree_node *n) {
    if (itree_node *p = n->up) {
      if (p->left == n)
        return p->left;
      if (p->right == n)
        return p->right;
      assert(!"tree_node has no parent");
    } else
      return root_;
  }

  static itree_node *itree_maximum(itree_node *x) {
    while (itree_node *r = x->right)
      x = r;
    return x;
  }
  static itree_node *itree_minimum(itree_node *x) {
    while (itree_node *l = x->left)
      x = l;
    return x;
  }
  static itree_node *itree_successor(const itree_node *);
  static itree_node *itree_predecessor(const itree_node *);
  void itree_insert(itree_node *);
  void itree_delete(itree_node *z);
  void __itree_check() const;
#if ITREE_CHECK
  void itree_check() const { __itree_check(); }
#else  // !ITREE_CHECK
  void itree_check() const {}
#endif // !ITREE_CHECK
};
static_assert(alignof(itree_base) >= 2, "Must co-opt LSB of pointer");

} // namespace detail

class itree_entry : private detail::itree_node {
  void move_from(itree_entry &n) {
    if (detail::itree_base *b = n.base()) {
      *static_cast<detail::itree_node *>(this) = n;
      b->pointer_to(&n) = this;
      if (left)
        left->up = this;
      if (right)
        right->up = this;
      n.base(nullptr);
    }
  }
  void destroy() {
    if (is_linked())
      unlink();
  }

public:
  itree_entry() {}
  itree_entry(itree_entry &&n) { move_from(n); }
  itree_entry &operator=(itree_entry &&n) {
    destroy();
    move_from(n);
    return *this;
  }
  //! If object is in a tree, the destructor removes it.
  ~itree_entry() { destroy(); }
  //! Returns true if the object is in a tree.
  bool is_linked() const { return in_tree(); }
  //! Removes the object from a tree.  Crashes if the object is not in
  //! a tree.
  void unlink() {
    detail::itree_base *b = base();
    assert(b);
    b->itree_delete(this);
  }
  template <typename V, auto Link, typename Compare> friend class itree_core;
};

template <typename V, auto Link, typename C>
class itree_core : detail::itree_base {
  // The trick is that we do everything in terms of pointers to
  // itree_entry fields in structures, and then convert back to the
  // base structure only when we need it.  For that to work, the
  // itree_entry cannot be in a virtual base class.  These functions
  // are used to do the backtranslation.
  static V *to_value(detail::itree_node *n) {
    return n ? static_cast<V *>(
                   struct_from_field(entry_ptr, static_cast<itree_entry *>(n)))
             : nullptr;
  }
  static const V *to_value(const detail::itree_node *n) {
    return n ? static_cast<const V *>(struct_from_field(
                   entry_ptr, static_cast<const itree_entry *>(n)))
             : nullptr;
  }

  bool cmp(const detail::itree_node *a,
           const detail::itree_node *b) const override final {
    return C()(*to_value(a), *to_value(b));
  }

public:
  using value_type = V;
  using compare_type = C;
  static constexpr itree_entry V::*entry_ptr = Link;

  //! Throws an assertion failure when the tree is non-empty.  You may
  //! wish to call remove_all() or delete_all() before destroying.
  ~itree_core() { assert(empty()); }

  //! Return \c true if tree contains no object.
  bool empty() const { return !root_; }
  //! Insert a new object into the tree.  The object must not already
  //! be in a tree that uses the same link.
  void insert(value_type *v) {
    itree_insert(&(v->*entry_ptr));
    itree_check();
  }
  //! Remove an object from the tree.  The object must be in this
  //! tree.  The removed object can be inserted into another tree.  A
  //! removed object can be deleted as well, but note you can also
  //! just delete an object and the \c itree_entry's destructor will
  //! automatically remove it from any tree.
  void remove(value_type *v) {
    using namespace detail;
    itree_delete(&(v->*entry_ptr));
    itree_check();
  }

  //! Return the next element in in-order traversal, or \c nullptr if
  //! \c v is the last element in the tree.
  static value_type *next(value_type *v) {
    return to_value(itree_successor(&(v->*entry_ptr)));
  }
  static const value_type *next(const value_type *v) {
    return to_value(itree_successor(&(v->*entry_ptr)));
  }
  //! Return the previous element in in-order traversal, or \c nullptr
  //! if \c v is the previous element in the tree.
  static value_type *prev(value_type *v) {
    return to_value(itree_predecessor(&(v->*entry_ptr)));
  }
  static const value_type *prev(const value_type *v) {
    return to_value(itree_predecessor(&(v->*entry_ptr)));
  }

  //! Return the \c itree_core containing value \v, or \c nullptr if
  //! \v is not in an \c itree_core.
  static itree_core *container_of(value_type *v) {
    return static_cast<itree_core *>((v->*entry_ptr).base());
  }

  //! Return the first object in the tree, or \c nullptr if the tree is empty.
  value_type *min() { return root_ ? to_value(itree_minimum(root_)) : nullptr; }
  const value_type *min() const {
    return root_ ? to_value(itree_minimum(root_)) : nullptr;
  }
  //! Return the last object in the tree, or \c nullptr if the tree is empty.
  value_type *max() { return root_ ? to_value(itree_maximum(root_)) : nullptr; }
  const value_type *max() const {
    return root_ ? to_value(itree_maximum(root_)) : nullptr;
  }

  //! Return the root of the tree, or \c nullptr if the tree is empty.
  value_type *root() { return to_value(root_); }
  //! Return the left child of a node, or \c nullptr.
  static value_type *left(value_type *v) {
    return to_value((v->*entry_ptr).left);
  }
  //! Return the right child of a node, or \c nullptr.
  static value_type *right(value_type *v) {
    return to_value((v->*entry_ptr).right);
  }
  //! Return the parent of a node, or \c nullptr if the node is the
  //! root.
  static value_type *up(value_type *v) { return to_value((v->*entry_ptr).up); }

  //! Returns the first object in the tree equal to a key by a
  //! comparison function, or \c nullptr if there is no such key.
  //! \arg \c k is the key to compare to.  \arg \c cc is the
  //! comparison functor, which must support both a <tt>bool
  //! operator()(const K&, const value_type&) const</tt> and a
  //! <tt>bool operator()(const value_type&, const K&) const</tt>.
  //!
  //! Note the default comparison functor is just the final template
  //! argument \c C.  \c itree supplies a type that works with both
  //! keys and values.
  template <typename K, typename CC = C>
  value_type *find(const K &k, const CC cc = compare_type()) {
    value_type *ret{nullptr};
    for (value_type *v = root(); v;) {
      if (cc(k, *v))
        v = left(v);
      else if (cc(*v, k))
        v = right(v);
      else {
        // Look for first matching value
        ret = v;
        v = left(v);
      }
    }
    return ret;
  }
  template <typename K, typename CC = C>
  const value_type *find(const K &k, const CC cc = compare_type()) const {
    return const_cast<itree_core *>(this)->find(k, cc);
  }

  //! Return the first tree element \c v such that <tt>cc(*v, k) ==
  //! false</tt>, or \c nullptr if the tree contains no such
  //! element.  Ignoring type errors, \c v is the first element such
  //! that <tt>!(*v < k)</tt> or <tt>k <= *v</tt>.  Type \c CC must
  //! have a <tt>bool operator()(const value_type&, const K&)
  //! const</tt> method (like \c std::less).  The arguments are the
  //! same as \c itree_core::find.
  template <typename K, typename CC = C>
  value_type *lower_bound(const K &k, const CC cc = compare_type()) {
    value_type *vv = nullptr; // lowest seen such that not(vv < k)
    for (value_type *v = root(); v;) {
      if (cc(*v, k))
        v = right(v);
      else {
        vv = v;
        v = left(v);
      }
    }
    return vv;
  }
  //! Return the last tree element \c v such that <tt>cc(k, *v) ==
  //! false</tt>, or \c nullptr if the tree contains no such
  //! element.  Ignoring type errors, \c v is the last element such
  //! that <tt>!(k < *v)</tt> or <tt>*v <= k</tt>.  This is exactly
  //! symmetric with \c lower_bound in that both return elements
  //! within the range.
  //!
  //! The main reason for this method is if you want to find the
  //! last element not greater than some target.  The reason to
  //! supply \c upper_bound_prev() in addition to \c upper_bound()
  //! is that you can't walk the \c end() iterator backwards.
  //! Hence, if you need the last value before some maximum you have
  //! to use \c upper_bound_prev() rather than taking the \c
  //! prev(upper_bound()).
  //!
  //! If you are looping between \c lower_bound() and \c
  //! upper_bound_prev(), you must process both elements if they are
  //! distinct.  For that reason, when processing the half-open
  //! interval [L,H), is is more useful to use \c lower_bound() for
  //! both ends of the range: i.e., start your scan at \c
  //! lower_bound(L) and end when you reach \c lower_bound(H)
  //! (without processing the latter).
  template <typename K, typename CC = C>
  value_type *upper_bound_prev(const K &k, const CC cc = compare_type()) {
    value_type *vv = nullptr; // highest seen such that not(vv < k)
    for (value_type *v = root(); v;) {
      if (cc(k, *v))
        v = left(v);
      else {
        vv = v;
        v = right(v);
      }
    }
    return vv;
  }

  //! Return the first tree element \c v such that <tt>!cc(k, *v) &&
  //! !cc(*v, k)</tt>.  If there is no element in the tree with key
  //! equivalent to \c k, then it returns the same value as \c
  //! lower_bound(), but when \c k is in the tree, returns the first
  //! element with a key above \c k.
  template <typename K, typename CC = C>
  value_type *upper_bound(const K &k, const CC cc = compare_type()) {
    if (value_type *vv = upper_bound_prev(k, cc))
      return next(vv);
    return nullptr;
  }

private:
  static value_type *min_postorder(value_type *n) {
    value_type *nn{nullptr}; // last non-null n
    while (n) {
      nn = n;
      n = left(nn);
      if (!n)
        n = right(nn);
    }
    return nn;
  }
  value_type *min_postorder() { return min_postorder(root()); }
  static value_type *next_postorder(value_type *n) {
    value_type *nn = up(n), *nnr;
    if (nn && left(nn) == n && (nnr = right(nn)))
      return min_postorder(nnr);
    return nn;
  }

public:
  //! Empty the tree by removing but not deleting every element.
  void remove_all() {
    value_type *nn = min_postorder();
    for (value_type *n = min_postorder(), *nn; n; n = nn) {
      nn = next_postorder(n);
      (n->*entry_ptr).base(nullptr);
    }
    root_ = nullptr;
  }
  //! Empty the tree by removing and deleting every element.
  void delete_all() {
    for (value_type *n = min_postorder(), *nn; n; n = nn) {
      nn = next_postorder(n);
      (n->*entry_ptr).base(nullptr);
      delete n;
    }
    root_ = nullptr;
  }

  //! The iterators are primarily to support range-for syntax; see the
  //! comment at begin().
  using iterator = detail::fake_iterator<V, itree_core>;
  using const_iterator = detail::fake_iterator<const V, itree_core>;
  //! "Fake" begin function to make range-for syntax work.  Note that
  //! this function takes log(n) time (though \c end() is constant
  //! time), whereas STL containers are supposed to have constant-time
  //! \c begin() methods.  Other than for range-for syntax, you are
  //! better off using \c min(), \c max(), \c next(), and \c prev()
  //! for navigating trees.
  iterator begin() { return min(); }
  const_iterator begin() const { return min(); }
  //! Constant time end iterator (that cannot be walked backwards).
  iterator end() { return nullptr; }
  const_iterator end() const { return nullptr; }
};

template <auto KP, auto Link, typename V, typename C>
class itree : public itree_core<V, Link, detail::field_compare<KP, C>> {
public:
  // Note:  C is a comparison type for the keys, while compare_type
  // compares objects of type V with each other and with keys.
  using compare_type = detail::field_compare<KP, C>;
  using core_type = itree_core<V, Link, compare_type>;
  using key_type = typename compare_type::key_type;
  using typename core_type::value_type;

  //! Return the first element of the tree matching key \c k, or \c
  //! nullptr if there is no matching element.  Note that this behaves
  //! differently from \c operator[] in STL containers, which allocate
  //! an element when one does not exist.  The difference is because
  //! \c itree never allocates memory.  Also note the return type,
  //! which is a pointer instead of a reference.
  value_type *operator[](const key_type &k) {
    return this->find(k, compare_type());
  }
  const value_type *operator[](const key_type &k) const {
    return this->find(k, compare_type());
  }
};

//! Easy way to declare \c operator< for a structure with multiple
//! fields.  For example:
//! \code
//! inline bool operator<(const mystruct &a, const mystruct &b) {
//!   return multiless(a, b, &mystruct::field1, &mystruct::field2);
//! }
//! \endcode
template <typename C, typename F, typename... Rest>
inline bool multiless(const C &a, const C &b, F C::*f, Rest... rest) {
  return std::less<F>{}(a.*f, b.*f) ||
         (!std::less<F>{}(b.*f, a.*f) && multiless(a, b, rest...));
}
template <typename C> inline bool multiless(const C &a, const C &b) {
  return false;
}
