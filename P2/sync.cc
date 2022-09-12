#include "thread.hh"
#include "timer.hh"

void Mutex::lock() {
  if (mine())
    throw SyncError("acquiring mutex already locked by this thread");

  // You need to implement the rest of this function
}

void Mutex::unlock() {
  if (!mine())
    throw SyncError("unlocking mutex not locked by this thread");

  // You need to implement the rest of this function
}

bool Mutex::mine() {
  // You need to implement this function
  return true;
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
