#pragma once

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>

using std::size_t;

// The stack pointer holds a pointer to a stack element, where most
// stack elements are the size of a uintptr_t.
using sp_t = std::uintptr_t *;

// Handy alias for allocating bytes that will automatically be freed
// when Bytes goes out of scope.  Usage:
//     Bytes mem{new char[8192]};
using Bytes = std::unique_ptr<char[]>;

class Thread {
public:
  // Create a new thread that will run a given function and will
  // have a given stack size.
  static void create(std::function<void()> main, size_t stack_size = 8192);

  // Return the currently running thread.
  static Thread *current();

  // Stop executing the current thread and switch (spelled "swtch"
  // since switch is a C++ keyword) to the next scheduled thread.
  // The current thread should already have been either re-scheduled
  // or enqueued in some wait Queue before calling this.
  static void swtch();

  // Re-schedule the current thread and switch to the next scheduled
  // thread.
  static void yield();

  // Terminate the current thread.
  [[noreturn]] static void exit();

  // Initialize preemptive threading.  If this function is called
  // once at the start of the program, it should then preempt the
  // currently running thread every usec microseconds.
  static void preempt_init(std::uint64_t usec = 100'000);

  // Schedule a thread to run when the CPU is available.  (The
  // thread should not be in a queue when you call this function.)
  void schedule();

private:
  // Constructor that does not allocate a stack, for initial_thread only.
  Thread(std::nullptr_t);
  Thread(std::function<void()> func, size_t stack_size);
  ~Thread();

  static void invoke() {
    static_func();
    exit();
  }

  static void preempt_handler() { Thread::yield(); }

  static std::map<Thread *, std::function<void()>> thread2func;
  // A Thread object for the program's initial thread.
  static Thread *initial_thread;
  static Thread *current_thread;

  // Fill in other fields and/or methods that you need.
  static std::function<void()> static_func;
  Bytes stack;
  sp_t sp = nullptr;
};

// Throw this in response to incorrect use of synchronization
// primitives.  Example:
//    throw SyncError("must hold Mutex to wait on Condition");
struct SyncError : public std::logic_error {
  using std::logic_error::logic_error;
};

// A standard mutex providing mutual exclusion.
class Mutex {
public:
  Mutex() = default;
  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;
  void lock();
  void unlock();

  // True if the lock is held by the current thread
  bool mine();

private:
  // You must implement this object
};

// A condition variable with one small twist.  Traditionally you
// supply a mutex when you wait on a condition variable, and it is an
// error if you don't always supply the same mutex.  To avoid this
// error condition and simplify assertion checking, here you just
// supply the Mutex at the time you initialize the condition variable
// and it is implicit for all the other operations.
class Condition {
public:
  explicit Condition(Mutex &m) : m_(m) {}
  void wait();      // Go to sleep until signaled
  void signal();    // Signal at least one waiter if any exist
  void broadcast(); // Signal all waiting threads

private:
  Mutex &m_;

  // You need to implement this object
};

// An object that acquires a lock in its constructor and releases it
// in the destructor, so as to avoid the risk that you forget to
// release the lock.  An example:
//
//     if (LockGuard lg(my_mutex); true) {
//         // Do something while holding the lock
//     }
using LockGuard = std::lock_guard<Mutex>;
