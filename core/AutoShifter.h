#pragma once
#include <chrono>
#include <deque>
#include <mutex>

namespace rdm {
class AutoShifter {
private:
  struct Entry {
    std::chrono::steady_clock::time_point time;
    int                                   wasAccepted;
  };

  std::deque<Entry> queue_;
  mutable std::mutex mutex_;
  std::chrono::steady_clock::time_point prevCompTime_;

  unsigned int ldmCount_;
  bool isPrimary_;
  bool shouldSwitch_;
  double intervalSeconds_;

  void
  ResetInternal() noexcept;

public:

  AutoShifter(bool isPrimary, unsigned int ldmCount, double intervalSeconds) noexcept;

  ~AutoShifter() = default;

  AutoShifter(const AutoShifter&) = delete;
  AutoShifter&
  operator = (const AutoShifter&) = delete;

  void
  Init(bool isPrimary) noexcept;

  int
  SetLdmCount(unsigned int count) noexcept;

  int
  Process(int success, size_t size);

  bool
  ShouldSwitch() const noexcept;
};
}
