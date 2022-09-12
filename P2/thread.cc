#include <deque>
#include <unistd.h>

#include "stack.hh"
#include "thread.hh"
#include "timer.hh"

Thread *Thread::initial_thread = new Thread(nullptr);
Thread *Thread::current_thread = initial_thread;
std::map<Thread *, std::function<void()>> Thread::thread2func;
std::function<void()> Thread::static_func = [] {};
std::deque<Thread *> thread_queue;

// Create a placeholder Thread for the program's initial thread (which
// already has a stack, so doesn't need one allocated).
Thread::Thread(std::nullptr_t) {
  // You have to implement this; depending on the contents of your
  // Thread structure, an empty function body may be sufficient.
}

Thread::Thread(std::function<void()> func, size_t stack_size) {
  stack = Bytes{new char[stack_size]};
  sp = stack_init(stack.get(), stack_size, invoke);
}

Thread::~Thread() {}

void Thread::create(std::function<void()> main, size_t stack_size) {
  Thread *new_thread = new Thread(main, stack_size);
  thread_queue.push_back(new_thread);
  thread2func[new_thread] = main;
}

Thread *Thread::current() {
  // Replace the code below with your implementation.
  return current_thread;
}

void Thread::schedule() {
  for (auto ptr : thread_queue) {
    if (ptr == this)
      return;
  }
  thread_queue.push_back(this);
}

void Thread::swtch() {
  IntrGuard ig;

  Thread *prev = current_thread;
  current_thread = thread_queue.front();
  thread_queue.pop_front();
  static_func = thread2func[current_thread];
  stack_switch(&prev->sp, &current_thread->sp);
}

void Thread::yield() {
  current_thread->schedule();
  swtch();
}

void Thread::exit() {
  IntrGuard ig;

  Thread *prev = current_thread;
  current_thread = thread_queue.front();
  thread_queue.pop_front();
  static_func = thread2func[current_thread];
  stack_switch(&prev->sp, &current_thread->sp);

  std::abort(); // Leave this line--control should never reach here
}

void Thread::preempt_init(std::uint64_t usec) {
  // You have to implement this
}
