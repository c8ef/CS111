#include "timer.hh"

#include <cstdio>
#include <new>
#include <system_error>

#include <signal.h>
#include <sys/time.h>

// Throw an exception based on the current POSIX error number errno.
[[noreturn]] void threrror(const char *msg) {
  throw std::system_error(errno, std::system_category(), msg);
}

namespace {

// When zero, we should defer timer interrupts and not call
// timer_handler
volatile sig_atomic_t enabled_flag = 1;

// Non-zero when a timer event was deferred because intr_disabled was non-zero
volatile sig_atomic_t interrupted;

// The function we should invoke (with interrupts disabled) whenever
// the timer fires
std::function<void()> &timer_handler = *new std::function<void()>;

void timer_interrupt(int sig) {
  if (!enabled_flag) {
    interrupted = 1;
    return;
  }

  IntrGuard ig;

  // Re-enable timer signals.  Signal handlers start with signals
  // masked so as to avoid arbitrarily nested signal handlers that
  // could blow out the stack.  We can safely re-enable signals here
  // because the IntrGuard above ensures at most one more nested
  // signal will be delivered on the current stack.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, sig);
  sigprocmask(SIG_UNBLOCK, &mask, NULL);

  interrupted = 0;
  timer_handler();
}

} // anonymous namespace

bool intr_enabled() { return enabled_flag; }

void intr_enable(bool on) {
  enabled_flag = on;
  while (enabled_flag && interrupted) {
    IntrGuard ig; // Supposed to call handler w/o interrupts
    interrupted = 0;
    timer_handler();
  }
}

void timer_init(std::uint64_t usec, std::function<void()> handler) {
  if (handler && usec) {
    timer_handler = handler;
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = &timer_interrupt;
    if (sigaction(SIGALRM, &sa, nullptr) == -1)
      threrror("sigaction");

    itimerval itv;
    itv.it_interval.tv_sec = usec / 1'000'000;
    itv.it_interval.tv_usec = usec % 1'000'000;
    itv.it_value = itv.it_interval;
    if (setitimer(ITIMER_REAL, &itv, nullptr) == -1)
      threrror("setitimer");
  } else {
    itimerval itv;
    itv.it_interval.tv_sec = 0;
    itv.it_interval.tv_usec = 0;
    itv.it_value = itv.it_interval;
    if (setitimer(ITIMER_REAL, &itv, nullptr) == -1)
      threrror("setitimer");

    timer_handler = handler;
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_DFL;
    if (sigaction(SIGALRM, &sa, nullptr) == -1)
      threrror("sigaction");

    timer_handler = nullptr;
  }
}

void *operator new(size_t n) {
  IntrGuard ig;
  if (void *p = malloc(n))
    return p;
  throw std::bad_alloc{};
}

void operator delete(void *p) {
  IntrGuard ig;
  free(p);
}
