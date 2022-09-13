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
  std::mutex mutex;
};

Station::Station() : mutex() {}

void Station::load_train(int available) {
  // You need to implement this
}

void Station::wait_for_train() {
  // You need to implement this
}

void Station::seated() {
  // You need to implement this
}