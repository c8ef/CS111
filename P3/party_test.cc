/*
 * This file tests the implementation of the Party class in party.cc. You
 * shouldn't need to modify this file (and we will test your code against
 * an unmodified version). Please report any bugs to the course staff.
 *
 * Note that passing these tests doesn't guarantee that your code is correct
 * or meets the specifications given, but hopefully it's at least pretty
 * close.
 */

#include <atomic>
#include <cstdarg>
#include <functional>
#include <thread>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "party.cc"

// Interval for nanosleep corresponding to 1 ms.
struct timespec one_ms = {.tv_sec = 0, .tv_nsec = 1000000};

// Total number of guests that have been matched in this experiment.
std::atomic<int> matched;

// Total number of guests that have started execution in this experiment
// (used to ensure a particular ordering of guests, but it's not perfect).
std::atomic<int> started;

// Total number of errors detected. //
int num_errors;

/**
 * Verify whether a guest matched as expected and print info about
 * a match or error.
 * \param guest
 *      Name of a particular guest.
 * \param expected
 *      Expected match for that guest (empty string means no match expected
 *      yet).
 * \param actual
 *      Actual match for the guest so far: empty means no one has been
 *      matched to this guest yet.
 * \return
 *      1 means that expected and actual didn't match, zero means they did.
 */
int check_match(std::string guest, std::string expected, std::string actual) {
  if (actual == expected) {
    if (expected.length() > 0) {
      printf("%s received %s as its match\n", guest.c_str(), actual.c_str());
    }
    return 0;
  } else if (expected.length() == 0) {
    printf("Error: %s matched prematurely with %s\n", guest.c_str(),
           actual.c_str());
    return 1;
  } else if (actual.length() == 0) {
    printf("Error: %s was supposed to receive %s as match, but it "
           "hasn't matched yet\n",
           guest.c_str(), expected.c_str());
    return 1;
  } else {
    printf("Error: %s was supposed to receive %s as match, but it "
           "received %s instead\n",
           guest.c_str(), expected.c_str(), actual.c_str());
    return 1;
  }
}

/*
 * Wait for a given number of matches to occur.
 * \param count
 *      Wait until matched reaches this value
 * \param ms
 *      Return after this many milliseconds even if match hasn't
 *      reached the desired value.
 * \return
 *      True means the function succeeded, false means it failed.
 */
bool wait_for_matches(int count, int ms) {
  while (1) {
    if (matched >= count) {
      return true;
    }
    if (ms <= 0) {
      return false;
    }
    nanosleep(&one_ms, nullptr);
    ms -= 1;
  }
}

/**
 * This structure is shared among all of the threads participating in
 * the cond_fifo test.
 */
struct FifoState {
  std::mutex mutex;
  std::condition_variable cond;

  /* Number of threads that have waited on cond. */
  std::atomic<int> arrivals;

  /* Number of threads that have reawakened after waiting on cond. */
  std::atomic<int> departures;

  FifoState() : mutex(), cond(), arrivals(0), departures(0) {}
};

/**
 * This method is run as the top-level method in a thread as part of the
 * cond_fifo test. It waits on a condition variable and verifies that it
 * woke up in fifo order.
 * @param state
 *      Shared info for the test.
 */
void waiter(FifoState *state) {
  std::unique_lock lock(state->mutex);
  int my_order = state->arrivals;
  state->arrivals++;
  state->cond.wait(lock);
  if (state->departures != my_order) {
    int d = state->departures;
    printf("Error: arrival %d departed as %d\n", my_order, d);
  }
  state->departures++;
}

/**
 * This test isn't part of the official test suite; it's used just for
 * checking to see if condition variable wakeups are done in FIFO order.
 */
void cond_fifo() {
  FifoState state;
  const int NUM_THREADS = 100;
  std::thread *threads[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    threads[i] = new std::thread(waiter, &state);
  }
  while (state.arrivals != NUM_THREADS) /* Do nothing */
    ;
  for (int i = 0; i < NUM_THREADS; i++) {
    state.cond.notify_one();
    // If the line below is commented out, there will be many more
    // out-of-order events. This seems to suggest that a waking thread
    // doesn't get added back to the lock queue until after it wakes
    // up, so if many threads wake up about the same time, they will
    // race to determine who gets the lock first.
    while (state.departures <= i) /* Do nothing */
      ;
  }
  for (int i = 0; i < NUM_THREADS; i++) {
    threads[i]->join();
    delete threads[i];
  }
}

/**
 * Invokes the meet method on a Party, records result information.
 * \param party
 *      Party object to invoke.
 * \param name
 *      Name of entering guest.
 * \param my_sign
 *       Zodiac sign of entering guest.
 * \param other_sign
 *       Desired Zodiac sign for match.
 * \param other_name
 *       The name of the matching guest is stored here.
 */
void guest(Party *party, std::string name, int my_sign, int other_sign,
           std::string *other_name) {
  started++;
  *other_name = party->meet(name, my_sign, other_sign);
  matched++;
}

// Each method below tests a particular scenario.

void two_guests_perfect_match(void) {
  Party party1;
  std::string match_a, match_b;

  matched = 0;
  printf("guest_a arrives: sign 0, other_sign 5\n");
  std::thread guest_a(guest, &party1, "guest_a", 0, 5, &match_a);
  guest_a.detach();
  printf("guest_b arrives: sign 5, other_sign 0\n");
  std::thread guest_b(guest, &party1, "guest_b", 5, 0, &match_b);
  guest_b.detach();
  wait_for_matches(2, 100);
  check_match("guest_a", "guest_b", match_a);
  check_match("guest_b", "guest_a", match_b);
}

void return_in_order(void) {
  Party party1;
  std::string match_a, match_b, match_c, match_d, match_e, match_f;

  started = 0;
  matched = 0;
  printf("guest_a arrives: sign 1, other_sign 3\n");
  std::thread guest_a(guest, &party1, "guest_a", 1, 3, &match_a);
  guest_a.detach();
  while (started < 1) /* Do nothing */
    ;
  printf("guest_b arrives: sign 1, other_sign 3\n");
  std::thread guest_b(guest, &party1, "guest_b", 1, 3, &match_b);
  guest_b.detach();
  while (started < 2) /* Do nothing */
    ;
  printf("guest_c arrives: sign 1, other_sign 3\n");
  std::thread guest_c(guest, &party1, "guest_c", 1, 3, &match_c);
  guest_c.detach();
  while (started < 3) /* Do nothing */
    ;
  wait_for_matches(1, 10);
  if (check_match("guest_a", "", match_a) +
      check_match("guest_b", "", match_b) +
      check_match("guest_c", "", match_c)) {
    return;
  }

  printf("guest_d arrives: sign 3, other_sign 1\n");
  std::thread guest_d(guest, &party1, "guest_d", 3, 1, &match_d);
  guest_d.detach();
  wait_for_matches(2, 100);
  if (check_match("guest_a", "guest_d", match_a) +
      check_match("guest_b", "", match_b) +
      check_match("guest_c", "", match_c) +
      check_match("guest_d", "guest_a", match_d)) {
    return;
  }

  printf("guest_e arrives: sign 3, other_sign 1\n");
  std::thread guest_e(guest, &party1, "guest_e", 3, 1, &match_e);
  guest_e.detach();
  wait_for_matches(4, 100);
  if (check_match("guest_b", "guest_e", match_b) +
      check_match("guest_c", "", match_c) +
      check_match("guest_e", "guest_b", match_e)) {
    return;
  }

  printf("guest_f arrives: sign 3, other_sign 1\n");
  std::thread guest_f(guest, &party1, "guest_f", 3, 1, &match_f);
  guest_f.detach();
  wait_for_matches(6, 100);
  if (check_match("guest_c", "guest_f", match_c) +
      check_match("guest_f", "guest_c", match_f)) {
    return;
  }
}

void sign_matching(void) {
  Party party1;
  std::string match_a, match_b, match_c, match_d, match_e, match_f;

  matched = 0;
  printf("guest_a arrives: sign 1, other_sign 3\n");
  std::thread guest_a(guest, &party1, "guest_a", 1, 3, &match_a);
  guest_a.detach();
  printf("guest_b arrives: sign 2 other_sign 1\n");
  std::thread guest_b(guest, &party1, "guest_b", 2, 1, &match_b);
  guest_b.detach();
  printf("guest_c arrives: sign 3, other_sign 2\n");
  std::thread guest_c(guest, &party1, "guest_c", 3, 2, &match_c);
  guest_c.detach();
  wait_for_matches(1, 10);
  if (check_match("guest_a", "", match_a) +
      check_match("guest_b", "", match_b) +
      check_match("guest_c", "", match_c)) {
    return;
  }

  printf("guest_d arrives: sign 3, other_sign 1\n");
  std::thread guest_d(guest, &party1, "guest_d", 3, 1, &match_d);
  guest_d.detach();
  wait_for_matches(2, 100);
  if (check_match("guest_a", "guest_d", match_a) +
      check_match("guest_d", "guest_a", match_d)) {
    return;
  }

  printf("guest_e arrives: sign 2, other_sign 3\n");
  std::thread guest_e(guest, &party1, "guest_e", 2, 3, &match_e);
  guest_e.detach();
  wait_for_matches(4, 100);
  if (check_match("guest_c", "guest_e", match_c) +
      check_match("guest_e", "guest_c", match_e)) {
    return;
  }

  printf("guest_f arrives: sign 1, other_sign 2\n");
  std::thread guest_f(guest, &party1, "guest_f", 1, 2, &match_f);
  guest_f.detach();
  wait_for_matches(6, 100);
  if (check_match("guest_b", "guest_f", match_b) +
      check_match("guest_f", "guest_b", match_f)) {
    return;
  }
}

void single_sign(void) {
  Party party1;
  std::string match_a, match_b;

  matched = 0;
  printf("guest_a arrives: sign 2, other_sign 2\n");
  std::thread guest_a(guest, &party1, "guest_a", 2, 2, &match_a);
  guest_a.detach();
  printf("guest_b arrives: sign 2, other_sign 2\n");
  std::thread guest_b(guest, &party1, "guest_b", 2, 2, &match_b);
  guest_b.detach();
  wait_for_matches(2, 100);
  if (check_match("guest_a", "guest_b", match_a) +
      check_match("guest_b", "guest_a", match_b)) {
    return;
  }
}

void single_sign_many(void) {
  Party party1;
  std::string matches[10];
  matched = 0;
  for (int i = 0; i < 10; i++) {
    printf("guest %d arrives: sign 2, other_sign 2\n", i);
    std::thread guest_x(guest, &party1, std::to_string(i), 2, 2, &matches[i]);
    guest_x.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  wait_for_matches(10, 1000);
  for (int i = 0; i < 10; i += 2) {
    check_match(std::to_string(i), std::to_string(i + 1), matches[i]);
    check_match(std::to_string(i + 1), std::to_string(i), matches[i + 1]);
  }
}

void same_name(void) {
  Party party1;
  std::string match1;
  std::string match2;
  std::string match3;
  std::string match4;
  started = 0;
  matched = 0;
  printf("Zendaya (clone 1) arrives: sign 4, other_sign 5\n");
  std::thread guest1(guest, &party1, "Zendaya", 4, 5, &match1);
  guest1.detach();
  while (started < 1) /* Do nothing */
    ;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  printf("Zendaya (clone 2) arrives: sign 4, other_sign 5\n");
  std::thread guest2(guest, &party1, "Zendaya", 4, 5, &match2);
  guest2.detach();
  while (started < 2) /* Do nothing */
    ;
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  printf("Zendaya (clone 3) arrives: sign 5, other_sign 4\n");
  std::thread guest3(guest, &party1, "Zendaya", 5, 4, &match3);
  guest3.detach();
  wait_for_matches(2, 100);
  check_match("Zendaya (clone 1)", "Zendaya", match1);
  check_match("Zendaya (clone 3)", "Zendaya", match3);
  printf("Zendaya (clone 4) arrives: sign 5, other_sign 4\n");
  std::thread guest4(guest, &party1, "Zendaya", 5, 4, &match4);
  guest4.detach();
  wait_for_matches(4, 100);
  check_match("Zendaya (clone 2)", "Zendaya", match2);
  check_match("Zendaya (clone 4)", "Zendaya", match4);
}

void random_party() {
  // Chnage the constant below to change the size of the party
  int PEOPLE = 100;
  // Number of distinct signs to use (fewer means more collisions).
  int MAX_SIGNS = 4;
  std::string matches[PEOPLE];
  std::vector<std::pair<int, int>> random_list;
  std::vector<int> random_signs;
  Party party1;

  for (int i = 0; i < 12; i++) {
    random_signs.push_back(i);
  }
  std::random_shuffle(random_signs.begin(), random_signs.end());
  for (int i = 0; i < PEOPLE / 2; i++) {
    int sign1 = random_signs[rand() % MAX_SIGNS];
    int sign2 = random_signs[rand() % MAX_SIGNS];
    random_list.push_back(std::make_pair(sign1, sign2));
    random_list.push_back(std::make_pair(sign2, sign1));
  }
  std::random_shuffle(random_list.begin(), random_list.end());
  for (int i = 0; i < PEOPLE; i++) {
    int my_sign = random_list[i].first;
    int other_sign = random_list[i].second;
    printf("guest %d arrives: sign %d, other_sign %d\n", i, my_sign,
           other_sign);
    std::thread guest_n(guest, &party1, std::to_string(i), my_sign, other_sign,
                        &matches[i]);
    guest_n.detach();
  }
  wait_for_matches(PEOPLE, PEOPLE * 50);
  bool error = false;
  for (int i = 0; i < PEOPLE; i++) {
    if (matches[i].empty()) {
      printf("Error: guest %d didn't match\n", i);
      error = true;
      continue;
    }
    int other = stoi(matches[i]);
    if (matches[other].empty()) {
      printf("Error: guest %d matched to %d, but %d didn't match\n", i, other,
             other);
      error = true;
    } else if (matches[other] != std::to_string(i)) {
      printf("Error: guest %d matched to %d, but %d matched to %s\n", i, other,
             other, matches[other].c_str());
      error = true;
    }
    if (random_list[i].second != random_list[other].first) {
      printf("Error: guest %d mismatched to %d: wanted sign %d, got %d\n", i,
             other, random_list[i].second, random_list[other].first);
      error = true;
    }
  }
  if (!error) {
    printf("All guests matched successfully\n");
  }
}

int main(int argc, char *argv[]) {
  srand(getpid() ^ time(NULL));
  if (argc == 1) {
    printf("Available tests are:\n  two_guests_perfect_match\n  "
           "return_in_order\n  sign_matching\n  single_sign\n  "
           "single_sign_many\n  same_name\n  random_party\n  cond_fifo\n");
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "two_guests_perfect_match") == 0) {
      two_guests_perfect_match();
    } else if (strcmp(argv[i], "return_in_order") == 0) {
      return_in_order();
    } else if (strcmp(argv[i], "sign_matching") == 0) {
      sign_matching();
    } else if (strcmp(argv[i], "single_sign") == 0) {
      single_sign();
    } else if (strcmp(argv[i], "single_sign_many") == 0) {
      single_sign_many();
    } else if (strcmp(argv[i], "same_name") == 0) {
      same_name();
    } else if (strcmp(argv[i], "random_party") == 0) {
      random_party();
    } else if (strcmp(argv[i], "cond_fifo") == 0) {
      cond_fifo();
    } else {
      printf("No test named '%s'\n", argv[i]);
    }
  }
}