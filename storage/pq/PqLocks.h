#pragma once
#include <pthread.h>
#include <signal.h>

namespace rdm {

class ProductQueue;
class PqMutex {
private:
  pthread_mutex_t mutex_;
  bool isThreadSafe_;

public:
  explicit
  PqMutex(bool isThreadSafe);
  ~PqMutex();

  // Prevent copying or moving
  PqMutex(const PqMutex&) = delete;
  PqMutex&
  operator = (const PqMutex&) = delete;

  void
  lock(int& savedCancelState);
  void
  unlock(int savedCancelState);
};

class ThreadLock {
private:
  PqMutex& mutex_;
  int previousCancelState_{ 0 };

public:
  explicit
  ThreadLock(PqMutex& mtx);
  ~ThreadLock();

  ThreadLock(const ThreadLock&) = delete;
  ThreadLock&
  operator = (const ThreadLock&) = delete;
};

class ControlLock {
private:
  ProductQueue& pq_;
  int rflags_;
  int status_;
  sigset_t sav_set_{}; 
  bool signals_blocked_{ false }; 

public:
  ControlLock(ProductQueue& pq, int rflags);
  ~ControlLock();

  ControlLock(const ControlLock&) = delete;
  ControlLock&
  operator = (const ControlLock&) = delete;

  explicit
  operator bool () const { return status_ == 0; } // 0 == ENOERR

  int
  status() const { return status_; }
};
}
