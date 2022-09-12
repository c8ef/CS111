/*
 * This file tests the code in thread.cc and sync.cc. Run this program
 * with one or more test names as arguments (see main below for the
 * names of existing tests). Please report any bugs to the course staff.
 *
 * Note that passing these tests doesn't guarantee that your code is correct
 * or meets the specifications given, but hopefully it's at least pretty
 * close.
 */

#include <atomic>
#include <cstring>
#include <iostream>

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include "thread.hh"
#include "timer.hh"

/**
 * This function doesn't return until either a context switch has
 * occurred or many seconds have elapsed. Context switches are detected
 * by polling a shared variable holding an id for the most recent thread
 * to run: when this is no longer equal to our_id, it means we must have
 * been preempted and then later started running again.
 */
void wait_for_switch(std::atomic<int> *most_recent, int our_id) {
  struct timeval start, current;

  if (gettimeofday(&start, nullptr) != 0) {
    printf("gettimeofday failed: %s\n", strerror(errno));
  }
  while (most_recent->load() == our_id) {
    if (gettimeofday(&current, nullptr) != 0) {
      printf("gettimeofday failed: %s\n", strerror(errno));
    }
    if ((1'000'000 * (current.tv_sec - start.tv_sec) +
         (current.tv_usec - start.tv_usec)) > 5'000'000) {
      printf("5 seconds elapsed with no preemption\n");
      break;
    }
  }
}

void yield_to_self_test() {
  printf("main thread yielding\n");
  Thread::yield();
  printf("main thread yielding again\n");
  Thread::yield();
  printf("main thread back from second yield\n");
}

void yield_to_child_test() {
  Thread::create([] {
    printf("child thread running; about to yield\n");
    Thread::yield();
    printf("child thread running again; exiting\n");
  });
  printf("main thread yielding to child thread\n");
  Thread::yield();
  printf("main thread running, about to yield\n");
  Thread::yield();
  printf("main thread running, about to yield (but no other threads)\n");
  Thread::yield();
  printf("main thread back from yield\n");
}

void yield_many_test() {
  for (int i = 0; i < 10; i++) {
    Thread::create([i] { printf("child %d woke up (exiting now)\n", i); });
  }
  printf("main thread yielding\n");
  Thread::yield();
  printf("main thread back from yield\n");
}

void block_test() {
  Thread *child = nullptr;
  Thread::create([&child] {
    child = Thread::current();
    printf("child thread running; about to block\n");
    IntrGuard ig;
    Thread::swtch();
    printf("child thread woke up from block; exiting\n");
  });
  printf("main thread yielding to child\n");
  Thread::yield();
  printf("main thread yielding again (child still blocked)\n");
  Thread::yield();
  printf("main thread woke up; waking child, then yielding\n");
  child->schedule();
  Thread::yield();
  printf("main thread back from final yield\n");
}

void preempt_test() {
  // Id of most recent thread to start a time slice.
  std::atomic<int> most_recent(-1);
  Thread::preempt_init(100'000);

  Thread::create([&most_recent] {
    for (int i = 0; i < 2; i++) {
      most_recent.store(1);
      printf("child1 now running\n");
      wait_for_switch(&most_recent, 1);
    }
    most_recent.store(1);
    printf("child1 now running; exiting\n");
  });

  Thread::create([&most_recent] {
    for (int i = 0; i < 3; i++) {
      most_recent.store(2);
      printf("child2 now running\n");
      wait_for_switch(&most_recent, 2);
    }
    most_recent.store(2);
    printf("child2 now running; exiting\n");
  });

  for (int i = 0; i < 4; i++) {
    most_recent.store(0);
    printf("main now running\n");
    wait_for_switch(&most_recent, 0);
  }
  printf("main now running; finished\n");
}

void mutex_basic_test() {
  Mutex m;

  Thread::create([&m] {
    printf("child thread attempting to lock\n");
    m.lock();
    printf("child thread acquired lock; now unlocking\n");
    m.unlock();
  });
  m.lock();
  printf("main thread yielding to child while holding lock\n");
  Thread::yield();
  printf("main thread yielding again while holding lock\n");
  Thread::yield();
  printf("main thread releasing lock then trying to reacquire\n");
  m.unlock();
  m.lock();
  printf("main thread reaqcuired lock\n");
}

void mutex_many_threads_test() {
  Mutex m1, m2;

  for (int i = 0; i < 3; i++) {
    Thread::create([&m1, &m2, i] {
      printf("child %d locking m1\n", i);
      m1.lock();
      printf("child %d unlocking m1, locking m2\n", i);
      m1.unlock();
      m2.lock();
      printf("child %d locked m2; unlocking and exiting\n", i);
      m2.unlock();
    });
  }
  m1.lock();
  m2.lock();
  printf("main thread yielding to children while holding locks\n");
  Thread::yield();
  printf("main thread unlocking m1 then yielding\n");
  m1.unlock();
  Thread::yield();
  printf("main thread yielding again\n");
  Thread::yield();
  printf("main thread yielding again\n");
  Thread::yield();
  printf("main thread unlocking m2 then trying to lock m1\n");
  m2.unlock();
  m1.lock();
  printf("main thread unlocking m1, then trying to reacquire m2\n");
  m1.unlock();
  m2.lock();
  printf("main thread reaqcuired m2\n");
}

void cond_basic_test() {
  Mutex m;
  Condition c(m);

  Thread::create([&m, &c] {
    printf("child waiting on condition\n");
    m.lock();
    c.wait();
    printf("child wokeup from c.wait; exiting\n");
    m.unlock();
  });
  printf("main thread yielding to child\n");
  Thread::yield();
  printf("main thread locking mutex\n");
  m.lock();
  printf("main thread signalling condition, then yielding "
         "(holding lock)\n");
  c.signal();
  Thread::yield();
  printf("main thread unlocking mutex, then yielding again\n");
  m.unlock();
  Thread::yield();
  printf("main thread woke up from yield, signalling again\n");
  m.lock();
  c.signal();
  m.unlock();
  printf("main thread yielding one last time\n");
  Thread::yield();
  printf("main thread back from final yield\n");
}

void two_conds_test() {
  Mutex m;
  Condition c1(m), c2(m);

  Thread::create([&m, &c1] {
    printf("child 1 waiting on condition 1\n");
    m.lock();
    c1.wait();
    printf("child 1 wokeup from c1.wait; waiting again\n");
    c1.wait();
    printf("child 1 wokeup again; exiting\n");
    m.unlock();
  });

  Thread::create([&m, &c2] {
    printf("child 2 waiting on condition 2\n");
    m.lock();
    c2.wait();
    printf("child 2 wokeup from wait; exiting\n");
    m.unlock();
  });
  printf("main thread yielding to children\n");
  Thread::yield();
  printf("main thread signaling condition 1, then yielding\n");
  m.lock();
  c1.signal();
  m.unlock();
  Thread::yield();
  printf("main thread broadcasting condition 1, then yielding\n");
  m.lock();
  c1.broadcast();
  m.unlock();
  Thread::yield();
  printf("main thread signaling condition 2, then yielding\n");
  m.lock();
  c2.signal();
  m.unlock();
  Thread::yield();
  printf("main thread woke up from yield\n");
}

void broadcast_test() {
  Mutex m;
  Condition c(m);

  for (int i = 0; i < 5; i++) {
    Thread::create([&m, &c, i] {
      printf("child %d waiting on condition\n", i);
      m.lock();
      c.wait();
      printf("child %d wokeup after wait; exiting\n", i);
      m.unlock();
    });
  }
  printf("main thread yielding to children\n");
  Thread::yield();
  printf("main thread broadcasting condition 1, then yielding\n");
  m.lock();
  c.broadcast();
  m.unlock();
  Thread::yield();
  printf("main thread woke up from yield\n");
}

int main(int argc, char **argv) {
  if (argc == 1) {
    printf("Available tests are:\n  yield_to_self\n  yield_to_child\n  "
           "yield_many\n  block\n  preempt\n  "
           "mutex_basic\n  mutex_many_threads\n  cond_basic\n  "
           "two_conds\n  broadcast\n");
  }
  for (int i = 1; i < argc; i++) {
    // The tests below here are arranged in order from easiest to
    // hardest. The first tests are for Project 2 (thread dispatching).
    if (strcmp(argv[i], "yield_to_self") == 0) {
      yield_to_self_test();
    } else if (strcmp(argv[i], "yield_to_child") == 0) {
      yield_to_child_test();
    } else if (strcmp(argv[i], "yield_many") == 0) {
      yield_many_test();
    } else if (strcmp(argv[i], "block") == 0) {
      block_test();
    } else if (strcmp(argv[i], "preempt") == 0) {
      preempt_test();

      // Tests below here are for Project 4 (synchronization)
    } else if (strcmp(argv[i], "mutex_basic") == 0) {
      mutex_basic_test();
    } else if (strcmp(argv[i], "mutex_many_threads") == 0) {
      mutex_many_threads_test();
    } else if (strcmp(argv[i], "cond_basic") == 0) {
      cond_basic_test();
    } else if (strcmp(argv[i], "two_conds") == 0) {
      two_conds_test();
    } else if (strcmp(argv[i], "broadcast") == 0) {
      broadcast_test();
    } else {
      printf("No test named '%s'\n", argv[i]);
    }
  }
}
