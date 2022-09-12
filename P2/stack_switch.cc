#include "stack.hh"

#if __x86_64

/*
  The x86_64 architecture has the following calling convention:

  [integer class] argument passing:
  - First six arguments: go in %rdi, %rsi, %rdx, %rcx, %r8, %r9
  - Remaining arguments pushed to stack (last arg pushed first)

  return value: %rax

  callee-saved registers: %rbp, %rbx, %r12, %r13, %r14, %r15

  stack pointer: %rsp (also callee-saved)
*/

asm(R"(.text
	.global stack_switch
stack_switch:
	pushq %rbp
	pushq %rbx
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	movq %rsp,(%rdi)	# *prev_sp = %rsp
	movq (%rsi),%rsp	# %rsp = *next_sp

	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %rbx
	popq %rbp

	ret
)");

// 6 saved registers plus return address
const size_t stack_switch_height = 7;

const size_t stack_alignment_divisor = 16;
const size_t stack_alignment_remainder = 8;

#elif __i386

/*
  The i386 architecture has the following calling convention:

  argument passing: all pushed to stack (in reverse order)

  return value: %eax

  callee-saved registers: %ebp, %edi, %esi, %ebx

  stack pointer: %esp (callee-saved)
*/

asm(R"(.text
	.global stack_switch
stack_switch:
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %ebx

	movl 20(%esp), %eax	# %eax = prev_sp
	movl 24(%esp), %edx	# %edx = next_sp
	movl %esp,(%eax)	# *prev_sp = %esp
	movl (%edx),%esp	# %esp = *next_sp

	popl %ebx
	popl %esi
	popl %edi
	popl %ebp

	ret
)");

// 4 saved registers + return address
const size_t stack_switch_height = 5;

const size_t stack_alignment_divisor = 16;
const size_t stack_alignment_remainder = 0;

#else
#error "unsupported architecture"
#endif
