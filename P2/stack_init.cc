#include <cassert>
#include <unistd.h>

#include "stack.hh"

// Size of a machine word (4 on a 32-bit machine, 8 on 64-bit).
static constexpr std::size_t mword_size = sizeof(std::uintptr_t);

[[noreturn]] static void stack_underflow() {
  static const char msg[] = "returned off the top of a thread stack\n";
  if (write(2, msg, sizeof(msg) - 1)) {
    /* Ignore write errors. */
  }
  std::abort();
}

sp_t stack_init(void *_stack, size_t size, void (*start)()) {
  uintptr_t stack{uintptr_t(_stack)};

  // Bail on ridiculously small stack sizes
  if (size < 2 * (mword_size * stack_switch_height + stack_alignment_divisor +
                  mword_size))
    throw std::domain_error("stack too small");

  // Align the stack bottom
  unsigned rem = stack % mword_size;
  if (rem) {
    int n = mword_size - rem;
    stack += n;
    size -= n;
  }

  // Leave room for stack_underflow
  size -= mword_size;

  // Align the stack top
  rem = (stack + size) % stack_alignment_divisor;
  if (rem < stack_alignment_remainder)
    size -= stack_alignment_divisor;
  size = size + stack_alignment_remainder - rem;

  sp_t top = reinterpret_cast<sp_t>(stack + size);
  assert(uintptr_t(top) % stack_alignment_divisor == stack_alignment_remainder);
  top[0] = std::uintptr_t(stack_underflow);
  top[-1] = std::uintptr_t(start);
  return top - stack_switch_height;
}
