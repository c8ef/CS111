#include "thread.hh"
#include "timer.hh"

void Mutex::lock() {
  if (mine())
    throw SyncError("acquiring mutex already locked by this thread");

  // You need to implement the rest of this function
  IntrGuard ig;

  if (lock_ == 0) {
    lock_ = 1;
    curr_ = Thread::current();
    return;
  }
  block_queue_.push(Thread::current());
  Thread::swtch();
}

void Mutex::unlock() {
  if (!mine())
    throw SyncError("unlocking mutex not locked by this thread");

  // You need to implement the rest of this function
  IntrGuard ig;

  if (block_queue_.size() == 0) {
    lock_ = 0;
    curr_ = nullptr;
  } else {
    curr_ = block_queue_.front();
    block_queue_.pop();
    curr_->schedule();
  }
}

bool Mutex::mine() {
  // You need to implement this function
  IntrGuard ig;
  return curr_ == Thread::current();
}

void Condition::wait() {
  if (!m_.mine())
    throw SyncError("Condition::wait must be called with mutex locked");

  // You need to implement the rest of this function
}

void Condition::signal() {
  if (!m_.mine())
    throw SyncError("Condition::signal must be called with mutex locked");

  // You need to implement the rest of this function
}

void Condition::broadcast() {
  if (!m_.mine())
    throw SyncError("Condition::broadcast must be called "
                    "with mutex locked");

  // You need to implement the rest of this function
}
