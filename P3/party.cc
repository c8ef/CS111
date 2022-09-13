#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

// The total number of Zodiac signs
static const int NUM_SIGNS = 12;

// Represents one party, which is capable of matching guests according
// to their zodiac signs.
class Party {
public:
  Party();
  std::string meet(std::string &my_name, int my_sign, int other_sign);

private:
  // Synchronizes access to this structure.
  std::mutex mutex_;
  std::queue<std::string> party_map_[12][12]{};
  std::condition_variable cond_map_[12][12]{};
};

Party::Party() : mutex_() {}

std::string Party::meet(std::string &my_name, int my_sign, int other_sign) {
  std::unique_lock lock(mutex_);
  if (my_sign != other_sign) {
    party_map_[my_sign][other_sign].push(my_name);
    cond_map_[my_sign][other_sign].notify_one();
  }

  while (my_sign != other_sign && party_map_[other_sign][my_sign].size() == 0)
    cond_map_[other_sign][my_sign].wait(lock);
  std::string ret = party_map_[other_sign][my_sign].front();
  party_map_[other_sign][my_sign].pop();
  return ret;
}
