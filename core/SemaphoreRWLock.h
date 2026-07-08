#pragma once

#include <sys/types.h>
#include <string>

namespace rdm {
class SemaphoreRWLock {
private:
  int fd_{ -1 };
  std::string lockFilePath_;
  unsigned numReadLocks_{ 0 };
  unsigned numWriteLocks_{ 0 };

  int
  PerformLock(short lockType);

public:
  SemaphoreRWLock() = default;
  ~SemaphoreRWLock();

  SemaphoreRWLock(const SemaphoreRWLock&) = delete;
  SemaphoreRWLock&
  operator = (const SemaphoreRWLock&) = delete;

  int
  Create(const std::string& name);
  int
  Attach(const std::string& name);
  int
  DestroyOSResource();
  static int
  DeleteByName(const std::string& name);

  int
  LockRead();
  int
  LockWrite();
  int
  Unlock();

  bool
  IsValid() const { return fd_ != -1; }
};

class ScopedReadLock {
private:
  SemaphoreRWLock& lock_;
  bool locked_;
public:
  explicit
  ScopedReadLock(SemaphoreRWLock& lock);
  ~ScopedReadLock();
  bool
  IsLocked() const { return locked_; }
};

class ScopedWriteLock {
private:
  SemaphoreRWLock& lock_;
  bool locked_;
public:
  explicit
  ScopedWriteLock(SemaphoreRWLock& lock);
  ~ScopedWriteLock();
  bool
  IsLocked() const { return locked_; }
};
}
