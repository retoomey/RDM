/**
 * @file FileCache.h
 * @brief Manages the Least-Recently-Used (LRU) cache for pqact file descriptors.
 */
#pragma once

#include "IFileEntry.h"
#include <list>
#include <memory>
#include <vector>
#include <algorithm>

namespace rdm {
namespace pqact {
class FileCache {
private:
  std::list<std::unique_ptr<IFileEntry> > cache_;
  size_t maxEntries_;
  unsigned long maxTimeIdleSecs_;

public:
  FileCache(size_t maxEntries, unsigned long maxTimeIdleSecs = 21600); // 6 hours
  ~FileCache();

  void
  SyncAll(bool block);
  void
  RemoveAndFree(IFileEntry * entry);
  void
  CloseAll();
  void
  EvictLru(int skipFlags);

  template <typename T>
  T *
  GetOrCreate(const std::vector<std::string>& args, bool& isNew)
  {
    // 1. Use an iterator directly so we don't have to search for it again
    for (auto it = cache_.begin(); it != cache_.end(); ++it) {
      auto& entry = *it;

      // 2. FAST PATH: Compare the enum trait instead of dynamic_cast
      if ((entry->GetType() == T::TYPE) && entry->IsMatch(args)) {
        entry->Touch();
      
        // 3. FAST PATH: Splice directly using the iterator we already hold
        if (it != cache_.begin()) {
          cache_.splice(cache_.begin(), cache_, it);
        }
      
        isNew = false;
        // Because we moved 'it' to the front, we can safely grab front()
        return static_cast<T *>(cache_.front().get());
      }
    }

    while (cache_.size() >= maxEntries_) {
      EvictLru(0);
    }

    auto newEntry = std::make_unique<T>();
    if (newEntry->Open(args) != 0) {
      return nullptr;
    }

    isNew = true;
    T * rawPtr = newEntry.get();
    cache_.push_front(std::move(newEntry));
    return rawPtr;
}

};
} // namespace pqact
}
