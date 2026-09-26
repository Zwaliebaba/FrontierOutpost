#include "Lock.h"

#include <mutex>

namespace {
  /* Recursive, as SFML's mutex was: a critical section. */
  struct LockImpl : public LockT {
    std::recursive_mutex mutex;

    void Acquire() {
      mutex.lock();
    }

    void Release() {
      mutex.unlock();
    }
  };
}

Lock Lock_Create() {
  return new LockImpl;
}
