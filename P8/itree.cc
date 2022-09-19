/* Straight-forward C++ transliteration of red-black trees in Cormen,
 * Leisserson, Rivest, Stein.
 *
 * Basic invariants:
 *   - root is black
 *   - a red node cannot have red children
 *   - every path to any leaf traverses the same number of black nodes
 */

#include "itree.hh"
#include <algorithm>

namespace detail {

itree_node *itree_base::itree_successor(const itree_node *x) {
  assert(x->in_tree());
  if (x->right)
    return itree_minimum(x->right);
  itree_node *y = x->up;
  while (y && x == y->right) {
    x = y;
    y = y->up;
  }
  return y;
}

itree_node *itree_base::itree_predecessor(const itree_node *x) {
  assert(x->in_tree());
  if (x->left)
    return itree_maximum(x->left);
  itree_node *y = x->up;
  while (y && x == y->left) {
    x = y;
    y = y->up;
  }
  return y;
}

/*      x               y
 *     / \             / \
 *    A   y   =>      x   C
 *       / \         / \
 *      B   C       A   B
 */
void itree_base::left_rotate(itree_node *x) {
  itree_node *y = x->right;
  x->right = y->left;
  if (y->left)
    y->left->up = x;
  y->up = x->up;
  pointer_to(x) = y;
  y->left = x;
  x->up = y;
}

/*        x             y
 *       / \           / \
 *      y   C   =>    A   x
 *     / \               / \
 *    A   B             B   C
 */
void itree_base::right_rotate(itree_node *x) {
  itree_node *y = x->left;
  x->left = y->right;
  if (y->right)
    y->right->up = x;
  y->up = x->up;
  pointer_to(x) = y;
  y->right = x;
  x->up = y;
}

void itree_base::insert_fixup(itree_node *z) {
  itree_node *p;
  while ((p = z->up) && p->color() == itree_color::RED) {
    itree_node *pp = p->up; // Guaranteed to exist since root isn't RED
    if (p == pp->left) {
      itree_node *y = pp->right; // y is z's "uncle"
      if (y && y->color() == itree_color::RED) {
        // Since the uncle and parent are both red, re-color
        // them black and make the grandparent red.  Move z to
        // the grantparent and continue in case the
        // great-grandparent is also red.
        p->color(itree_color::BLACK);
        y->color(itree_color::BLACK);
        pp->color(itree_color::RED);
        z = pp;
      } else {
        // z's uncle is red.  Since p is red, z's sibling (p's
        // other child) must be black, since before the
        // insertion red p must have had two black (or nil)
        // children.
        if (z == p->right) {
          // p is a left child and z is a right child.
          // rotate tree so that both are now left children.
          // Since rotation makes the old z the parent of
          // the old p, also swap z and p.
          std::swap(z, p);
          left_rotate(z);
        }
        // Rotate to make p the parent of pp.  After rotation,
        // pp's children will be z's sibling and uncle from
        // before rotation, both of which were black.  Hence,
        // with two black children, we can make pp red.
        p->color(itree_color::BLACK);
        pp->color(itree_color::RED);
        right_rotate(pp);
      }
    } else {
      // mirror image of above
      itree_node *y = pp->left;
      if (y && y->color() == itree_color::RED) {
        p->color(itree_color::BLACK);
        y->color(itree_color::BLACK);
        pp->color(itree_color::RED);
        z = pp;
      } else {
        if (z == p->left) {
          std::swap(z, p);
          right_rotate(z);
        }
        p->color(itree_color::BLACK);
        pp->color(itree_color::RED);
        left_rotate(pp);
      }
    }
  }
  root_->color(itree_color::BLACK);
}

void itree_base::itree_insert(itree_node *z) {
  assert(!z->in_tree());
  itree_node *y = nullptr;
  itree_node *x = root_;
  while (x) {
    y = x;
    if (cmp(z, x))
      x = x->left;
    else
      x = x->right;
  }
  z->up = y;
  if (!y)
    root_ = z;
  else if (cmp(z, y))
    y->left = z;
  else
    y->right = z;
  z->left = z->right = nullptr;
  z->base(this);
  insert_fixup(z);
}

inline itree_color color(const itree_node *n) {
  return n ? n->color() : itree_color::BLACK;
}

void itree_base::delete_fixup(itree_node *x, itree_node *xp) {
  for (; xp && color(x) == itree_color::BLACK; xp = x->up) {
    // x is "doubly black" and not root; push one blackness up the tree
    if (x == xp->left) {
      itree_node *w = xp->right; // w != nil (black height must match x)
      // Arrange for x's sibling to be black
      if (w->color() == itree_color::RED) {
        w->color(itree_color::BLACK);
        xp->color(itree_color::RED);
        left_rotate(xp);
        w = xp->right;
      }
      if (color(w->left) == itree_color::BLACK &&
          color(w->right) == itree_color::BLACK) {
        // Turn x's sibling red then move extra blackness to parent
        w->color(itree_color::RED);
        x = xp;
      } else {
        // w is black and has at least one red child
        if (color(w->right) == itree_color::BLACK) {
          // Make w such that the its right child is sibling red
          w->left->color(itree_color::BLACK);
          w->color(itree_color::RED);
          right_rotate(w);
          w = xp->right;
        }
        // Turn that (right) red child black
        w->color(xp->color());
        xp->color(itree_color::BLACK);
        w->right->color(itree_color::BLACK);
        left_rotate(xp);
        break;
      }
    } else {
      // Mechanically-generated mirror image of previous code
      itree_node *w = xp->left;
      if (w->color() == itree_color::RED) {
        w->color(itree_color::BLACK);
        xp->color(itree_color::RED);
        right_rotate(xp);
        w = xp->left;
      }
      if (color(w->right) == itree_color::BLACK &&
          color(w->left) == itree_color::BLACK) {
        w->color(itree_color::RED);
        x = xp;
      } else {
        if (color(w->left) == itree_color::BLACK) {
          w->right->color(itree_color::BLACK);
          w->color(itree_color::RED);
          left_rotate(w);
          w = xp->left;
        }
        w->color(xp->color());
        xp->color(itree_color::BLACK);
        w->left->color(itree_color::BLACK);
        right_rotate(xp);
        break;
      }
    }
  }
  if (x)
    x->color(itree_color::BLACK);
}

void itree_base::itree_delete(itree_node *z) {
  assert(z->base() == this); // Check z is in this tree

  // z is the node to delete
  // y (below) is the node that replaces z
  // x is the lowest transplanted (which could be nil)
  // xp is x's parent, which we need to keep track of in case x is nil
  //    if x is the root then xp is nil
  itree_node *x, *xp = z->up;
  // y_original_color is color not reflected in tree
  itree_color y_original_color = z->color();

  if (!z->left)
    transplant(z, x = z->right);
  else if (!z->right)
    transplant(z, x = z->left);
  else {
    // y is the node taking z's place
    itree_node *y = itree_minimum(z->right); // y != nil && y->left == nil
    y_original_color = y->color();
    x = y->right;
    if (y != z->right) {
      // excise y from the tree if it isn't z's child
      xp = y->up;
      transplant(y, x);
      y->right = z->right;
      y->right->up = y;
    } else
      xp = y;
    transplant(z, y);
    y->left = z->left;
    y->left->up = y;
    y->color(z->color());
  }

  z->base(nullptr);
  if (y_original_color == itree_color::BLACK)
    delete_fixup(x, xp); // Means x has one "extra blackness"
}

void itree_base::check_node(itree_node *n, unsigned bh,
                            unsigned wanted_bh) const {
  if (!n) {
    // All paths from root to leaf have same number of black nodes.
    assert(bh == wanted_bh);
    return;
  }

  // Every node is either red or black.
  assert(n->color() == itree_color::RED || n->color() == itree_color::BLACK);

  if (n->color() == itree_color::BLACK)
    ++bh;

  // Check order, and that if a node is red both its children are black.
  if (itree_node *l = n->left) {
    assert(!cmp(n, l));
    if (n->color() == itree_color::RED)
      assert(l->color() == itree_color::BLACK);
    check_node(l, bh, wanted_bh);
  }
  if (itree_node *r = n->right) {
    assert(!cmp(r, n));
    if (n->color() == itree_color::RED)
      assert(r->color() == itree_color::BLACK);
    check_node(r, bh, wanted_bh);
  }
}

void itree_base::__itree_check() const {
  if (!root_)
    return;

  // The root is black.
  assert(root_->color() == itree_color::BLACK);

  unsigned black_height = 0;
  for (itree_node *n = root_; n; n = n->left)
    if (n->color() == itree_color::BLACK)
      ++black_height;

  check_node(root_, 0, black_height);
}

} // namespace detail
