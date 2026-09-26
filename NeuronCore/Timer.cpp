#include "Timer.h"

#include <chrono>

struct TimerData {
  std::chrono::steady_clock::time_point start;
};

Timer::Timer() {
  d = new TimerData;
  d->start = std::chrono::steady_clock::now();
}

Timer::~Timer() {
  delete d;
}

float Timer::GetElapsed() const {
  return std::chrono::duration<float>(std::chrono::steady_clock::now() - d->start).count();
}

void Timer::Reset() {
  d->start = std::chrono::steady_clock::now();
}
