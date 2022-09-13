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
  std::condition_variable leave_, capa_;

  int capacity_;
  int waiting_;
  int boarding_;
  int sitting_;
};

Station::Station()
    : mutex_(), leave_(), capa_(), capacity_(0), waiting_(0), boarding_(0),
      sitting_(0) {}

void Station::load_train(int available) {
  std::unique_lock lock(mutex_);
  capacity_ = available;

  if (capacity_ > 0)
    // wake up passengers according to capacity
    for (int i = 0; i < capacity_; ++i)
      capa_.notify_one();
  while (!((boarding_ == sitting_) && (sitting_ == capacity_ || waiting_ == 0)))
    leave_.wait(lock);
  // when a train leaves, restore state variables
  capacity_ = 0;
  sitting_ = 0;
  boarding_ = 0;
}

void Station::wait_for_train() {
  std::unique_lock lock(mutex_);
  ++waiting_;
  // always cares about waiting conditions to ensure some thread sleeping
  while (capacity_ == 0 || boarding_ >= capacity_)
    capa_.wait(lock);
  --waiting_;
  ++boarding_;
}

void Station::seated() {
  std::unique_lock lock(mutex_);
  ++sitting_;

  if ((boarding_ == sitting_) && (sitting_ == capacity_ || waiting_ == 0))
    leave_.notify_all();
}