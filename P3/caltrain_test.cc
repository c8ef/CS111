/*
 * This file tests the implementation of the Station class in station.cc. You
 * shouldn't need to modify this file (and we will test your code against
 * an unmodified version). Please report any bugs to the course staff.
 *
 * Note that passing these tests doesn't guarantee that your code is correct
 * or meets the specifications given, but hopefully it's at least pretty
 * close.
 */

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <functional>
#include <thread>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "caltrain.cc"

// Interval for nanosleep corresponding to 1 ms.
struct timespec one_ms = {.tv_sec = 0, .tv_nsec = 1000000};

// Number of passenger threads that have completed their call to
// wait_for_train (they may or may not have completed the call to seated).
std::atomic<int> boarding_threads;

// Incremented whenever load_train returns.
std::atomic<int> loaded_trains;

// Total number of errors detected. //
int num_errors;

/// Runs in a separate thread to...
void passenger(Station *station) {
  station->wait_for_train();
  boarding_threads++;
}

/// Runs in a separate thread to simulate the arrival of a train
/// \param station
///      Station where the train is arriving.
/// \param free_seats
///      Number of seats available on the train.
void load_train(Station *station, int free_seats) {
  station->load_train(free_seats);
  loaded_trains++;
}

/// Wait for anatomic variable to reach a given value.
/// \param var
///      The variable to watch.
/// \param count
///      Wait until @var reaches a value at least as great as this.
/// \param ms
///      Return after this many milliseconds even if @var hasn't
///      reached the desired value.
/// \return
///      True means the function succeeded, false means it failed.
bool wait_for(std::atomic<int> *var, int count, int ms) {
  while (1) {
    if (var->load() >= count) {
      return true;
    }
    if (ms <= 0) {
      return false;
    }
    nanosleep(&one_ms, nullptr);
    ms -= 1;
  }
}

void no_waiting_passengers(void) {
  Station station;

  loaded_trains = 0;
  printf("Full train arrives with no waiting passengers\n");
  std::thread train1(load_train, &station, 0);
  train1.detach();
  if (!wait_for(&loaded_trains, 1, 100)) {
    printf("load_train didn't return immediately\n");
  } else {
    printf("load_train returned\n");
  }

  printf("Train with 10 seats arrives with no waiting passengers\n");
  std::thread train2(load_train, &station, 10);
  train2.detach();
  if (!wait_for(&loaded_trains, 2, 100)) {
    printf("load_train didn't return immediately\n");
  } else {
    printf("load_train returned\n");
  }
}

void basic(void) {
  Station station;

  loaded_trains = 0;
  boarding_threads = 0;

  printf("Passenger arrives, begins waiting\n");
  std::thread pass1(passenger, &station);
  pass1.detach();

  // First train has no room.
  printf("Train arrives with no empty seats\n");
  std::thread train1(load_train, &station, 0);
  train1.detach();
  if (!wait_for(&loaded_trains, 1, 100)) {
    printf("load_train didn't return immediately");
    return;
  } else {
    printf("load_train returned, train left\n");
  }

  // Second train arrives with room.
  loaded_trains = 0;
  boarding_threads = 0;
  printf("Train arrives with 3 seats available\n");
  std::thread train2(load_train, &station, 3);
  train2.detach();
  wait_for(&boarding_threads, 1, 100);
  if (boarding_threads.load() == 1) {
    printf("Passenger started boarding\n");
  } else {
    printf("Error: passenger didn't return from wait_for_train\n");
    return;
  }
  if (loaded_trains.load() != 0) {
    printf("Error: train left before passenger finished boarding\n");
    return;
  }

  // Introduce a second passenger while the first one is boarding.
  printf("Second passenger arrives\n");
  std::thread pass2(passenger, &station);
  pass2.detach();
  wait_for(&boarding_threads, 2, 100);
  if (boarding_threads.load() == 2) {
    printf("Second passenger started boarding\n");
  } else {
    printf("Error: second passenger didn't return from wait_for_train\n");
    return;
  }
  if (loaded_trains.load() != 0) {
    printf("Error: train left before passengers finished boarding\n");
    return;
  }

  // Finished boarding one passenger at a time.
  printf("First passenger finishes boarding\n");
  station.seated();
  wait_for(&loaded_trains, 1, 1);
  if (loaded_trains.load() != 0) {
    printf("Error: train left before passengers finished boarding\n");
    return;
  }
  printf("Second passenger finishes boarding\n");
  station.seated();
  wait_for(&loaded_trains, 1, 100);
  if (loaded_trains.load() != 0) {
    printf("load_train returned, train left\n");
  } else {
    printf("Error: load_train didn't return after passengers "
           "finished boarding\n");
    return;
  }
}

void board_in_parallel(void) {
  Station station;

  loaded_trains = 0;
  boarding_threads = 0;

  printf("4 passengers arrive, begin waiting\n");
  std::thread pass1(passenger, &station);
  pass1.detach();
  std::thread pass2(passenger, &station);
  pass2.detach();
  std::thread pass3(passenger, &station);
  pass3.detach();
  std::thread pass4(passenger, &station);
  pass4.detach();

  // Wait for a bit to give all the passengers time to wait on
  // condition variables. This is technically a race, since it's
  // possible that one of the threads could be blocked from running
  // for longer than this amount of time, but it's the best we can
  // without additional information from the Station class. Thus
  // this test may occasionally fail.
  usleep(1000000);

  printf("Train arrives with 3 empty seats\n");
  std::thread train1(load_train, &station, 3);
  train1.detach();
  if (wait_for(&boarding_threads, 3, 100)) {
    printf("3 passengers began boarding\n");
  } else {
    printf("Error: expected 3 passengers to begin boarding, but actual "
           "number is %d\n",
           boarding_threads.load());
    return;
  }
  printf("2 passengers finished boarding\n");
  station.seated();
  station.seated();
  wait_for(&loaded_trains, 1, 10);
  if (loaded_trains.load() > 0) {
    printf("Error: lload_train returned too soon\n");
    return;
  }
  printf("Third passenger finished boarding\n");
  station.seated();
  wait_for(&loaded_trains, 1, 100);
  if (loaded_trains.load() != 0) {
    printf("load_train returned, train left\n");
  } else {
    printf("Error: load_train didn't return when train was full\n");
    return;
  }

  // Send another train for the last passenger.
  printf("Another train arrives with 10 empty seats\n");
  std::thread train2(load_train, &station, 10);
  train2.detach();
  wait_for(&boarding_threads, 4, 100);
  if (boarding_threads.load() == 4) {
    printf("Last passenger began boarding\n");
  } else {
    printf("Error: last passenger didn't begin boarding\n");
    return;
  }
  printf("Last passenger finished boarding\n");
  station.seated();
  if (wait_for(&loaded_trains, 2, 100)) {
    printf("load_train returned, train left\n");
  } else {
    printf("Error: load_train didn't return after passenger "
           "finished boarding\n");
  }
}

void randomized(void) {
  Station station;
  int errors = 0;

  // Create a large number of passengers waiting in the station.
  int total_passengers = 1000;
  printf("Starting randomized test with %d passengers\n", total_passengers);
  for (int i = 0; i < total_passengers; i++) {
    // Yes, I know this leaks memory.
    (new std::thread(passenger, &station))->detach();
  }

  int passengers_left = total_passengers;
  const int max_free_seats_per_train = 50;

  // Each iteration through this loop processes a train with random
  // capacity.
  while (passengers_left > 0) {
    int free_seats = rand() % max_free_seats_per_train;
    boarding_threads = 0;
    loaded_trains = 0;
    printf("Train entering station with %d free seats, %d waiting "
           "passengers\n",
           free_seats, passengers_left);
    std::thread train(load_train, &station, free_seats);
    train.detach();

    int expected_boarders = std::min(passengers_left, free_seats);
    int boarded = 0;
    while (1) {
      while (boarding_threads > boarded) {
        station.seated();
        boarded++;
        passengers_left--;
      }
      if (boarded >= expected_boarders) {
        break;
      }
      if (!wait_for(&boarding_threads, boarded + 1, 100)) {
        printf("Error: stuck waiting for passenger %d to start "
               "boarding\n",
               boarded);
        errors++;
        return;
      }
      if (loaded_trains.load() != 0) {
        printf("Error: load_train returned after only %d "
               "passengers finished boarding\n",
               boarded);
        errors++;
        return;
      }
    }
    wait_for(&loaded_trains, 1, 100);
    if (loaded_trains.load() != 1) {
      printf("Error: load_train didn't return after %d passengers "
             "boarded\n",
             boarded);
      errors++;
      return;
    }
    nanosleep(&one_ms, nullptr);
    if (expected_boarders != boarding_threads) {
      printf("Error: %d passengers started boarding (expected %d)\n",
             boarding_threads.load(), expected_boarders);
      errors++;
    }
  }
  printf("Test completed with %d errors\n", errors);
}

/*
 * This creates a bunch of threads to simulate arriving trains and passengers.
 */
int main(int argc, char *argv[]) {
  srand(getpid() ^ time(NULL));

  if (argc == 1) {
    printf("Available tests are:\n  no_waiting_passengers\n  basic\n  "
           "board_in_parallel\n  random\n");
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "no_waiting_passengers") == 0) {
      no_waiting_passengers();
    } else if (strcmp(argv[i], "basic") == 0) {
      basic();
    } else if (strcmp(argv[i], "board_in_parallel") == 0) {
      board_in_parallel();
    } else if (strcmp(argv[i], "random") == 0) {
      randomized();
    } else {
      printf("No test named '%s'\n", argv[i]);
    }
  }
}
