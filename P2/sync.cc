#include "thread.hh"
#include "timer.hh"

void Mutex::lock() {
  if (mine())
    throw SyncError("acquiring mutex already locked by this thread");

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
  IntrGuard ig;
  return curr_ == Thread::current();
}

void Condition::wait() {
  if (!m_.mine())
    throw SyncError("Condition::wait must be called with mutex locked");

  IntrGuard ig;
  m_.unlock();
  wait_queue_.push(Thread::current());
  Thread::swtch();
  // when waking up from waiting, it must acquire lock
  m_.lock();
}

void Condition::signal() {
  if (!m_.mine())
    throw SyncError("Condition::signal must be called with mutex locked");

  IntrGuard ig;
  if (wait_queue_.size() > 0) {
    Thread *curr = wait_queue_.front();
    wait_queue_.pop();
    curr->schedule();
  }
}

void Condition::broadcast() {
  if (!m_.mine())
    throw SyncError("Condition::broadcast must be called "
                    "with mutex locked");

  IntrGuard ig;
  while (wait_queue_.size() > 0) {
    Thread *curr = wait_queue_.front();
    wait_queue_.pop();
    curr->schedule();
  }
}
