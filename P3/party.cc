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
  Party() = default;
  std::string meet(std::string &my_name, int my_sign, int other_sign);

private:
  // Synchronizes access to this structure.
  std::mutex mutex_{};
  std::queue<std::string> party_map_[2 * NUM_SIGNS + 1][2 * NUM_SIGNS + 1]{};
  std::condition_variable cond_map_[2 * NUM_SIGNS + 1][2 * NUM_SIGNS + 1]{};
  int same_index_[NUM_SIGNS]{};
};

std::string Party::meet(std::string &my_name, int my_sign, int other_sign) {
  std::unique_lock lock(mutex_);
  if (my_sign == other_sign) {
    ++same_index_[my_sign];
    if (same_index_[my_sign] % 2 == 0)
      other_sign += NUM_SIGNS;
    else
      my_sign += NUM_SIGNS;
  }

  party_map_[my_sign][other_sign].push(my_name);
  cond_map_[my_sign][other_sign].notify_one();

  while (party_map_[other_sign][my_sign].size() == 0)
    cond_map_[other_sign][my_sign].wait(lock);
  std::string ret = party_map_[other_sign][my_sign].front();
  party_map_[other_sign][my_sign].pop();
  return ret;
}
