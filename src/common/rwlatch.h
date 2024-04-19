#pragma once

#include <climits>
#include <condition_variable>
#include <mutex>

class RWLatch {
 public:
  RWLatch() : read_count_(0), write_count_(0){};
  ~RWLatch() { std::lock_guard<std::mutex> guard(mutex_); }

  void RLock();

  void RUnlock();

  void WLock();

  void WUnlock();

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  int read_count_;
  int write_count_;

};