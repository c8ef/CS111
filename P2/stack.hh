#pragma once

#include "thread.hh"

// Initializes a stack so that the first time you switch to it, the
// function start will run.  Returns the initial stack pointer with
// which you must start the thread using the initialized stack.
sp_t stack_init(void *stack, size_t bytes, void (*start)());

// Saves registers, stores the stack pointer in *prev_sp, then loads
// the stack pointer from *next_sp and restores registers.
extern "C" void stack_switch(sp_t *prev_sp, const sp_t *next_sp);

// At the time switch_stack actually switches stacks, the number of
// uintptr_t-sized stack elements from the stack pointer through
// switch_stack's return address.
extern const size_t stack_switch_height;

// The minimum alignment of the top of the stack in bytes.
// (stacktop % stack_alignment_divisor) must equal
// stack_alignment_remainder after a call instruction when entring a
// function).
extern const size_t stack_alignment_divisor;
extern const size_t stack_alignment_remainder;
