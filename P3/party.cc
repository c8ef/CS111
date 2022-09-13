#include <condition_variable>
#include <mutex>

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
  std::mutex mutex;
};

Party::Party() : mutex() {}

std::string Party::meet(std::string &my_name, int my_sign, int other_sign) {
  // You need to implement this
  return "??";
}
