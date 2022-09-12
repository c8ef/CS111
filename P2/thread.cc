#include <unistd.h>

#include "stack.hh"
#include "thread.hh"
#include "timer.hh"

Thread *Thread::initial_thread = new Thread(nullptr);

// Create a placeholder Thread for the program's initial thread (which
// already has a stack, so doesn't need one allocated).
Thread::Thread(std::nullptr_t) {
  // You have to implement this; depending on the contents of your
  // Thread structure, an empty function body may be sufficient.
}

Thread::~Thread() {
  // You have to implement this
}

void Thread::create(std::function<void()> main, size_t stack_size) {
  // You have to implement this
}

Thread *Thread::current() {
  // Replace the code below with your implementation.
  return nullptr;
}

void Thread::schedule() {
  // You have to implement this
}

void Thread::swtch() {
  // You have to implement this
}

void Thread::yield() {
  // You have to implement this
}

void Thread::exit() {
  // You have to implement this

  std::abort(); // Leave this line--control should never reach here
}

void Thread::preempt_init(std::uint64_t usec) {
  // You have to implement this
}
