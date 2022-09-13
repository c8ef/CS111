#include <condition_variable>
#include <mutex>

class Station {
public:
  Station();
  void load_train(int count);
  void wait_for_train();
  void seated();

private:
  // Synchronizes access to all information in this object.
  std::mutex mutex_;
  std::condition_variable full_, seat_;

  int waiting_num_;
  int ready_to_aboard_;
  int available_seat_;
};

Station::Station()
    : mutex_(), full_(), seat_(), waiting_num_(0), ready_to_aboard_(0),
      available_seat_(0) {}

void Station::load_train(int available) {
  std::unique_lock lock(mutex_);
  available_seat_ += available;

  if (available_seat_ != 0)
    seat_.notify_all();

  while (!(waiting_num_ == 0 || available_seat_ == 0))
    full_.wait(lock);
}

void Station::wait_for_train() {
  std::unique_lock lock(mutex_);
  waiting_num_++;

  while (!(available_seat_ != 0))
    seat_.wait(lock);
}

void Station::seated() {
  std::unique_lock lock(mutex_);
  --waiting_num_;
  --available_seat_;

  if (available_seat_ == 0 || waiting_num_ == 0)
    full_.notify_all();
}