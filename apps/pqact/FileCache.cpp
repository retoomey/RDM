#include "FileCache.h"
#include "Log.h"
#include <ctime>

namespace rdm {
namespace pqact {

FileCache::FileCache(size_t maxEntries, unsigned long maxTimeIdleSecs)
    : maxEntries_(maxEntries), maxTimeIdleSecs_(maxTimeIdleSecs) {}

FileCache::~FileCache() {
    CloseAll();
}

void FileCache::CloseAll() {
    // Unique_ptr automatically calls destructors which call Close()
    cache_.clear();
}

void FileCache::SyncAll(bool block) {
    time_t now = std::time(nullptr);
    for (auto it = cache_.begin(); it != cache_.end(); ) {
        IFileEntry* entry = it->get();
        bool remove = false;

        if (entry->IsFlagSet(FL_NEEDS_SYNC)) {
            if (entry->Sync(block) != 0) {
                LogError("Deleting failed entry: {}", entry->GetPath());
                remove = true;
            }
        }

        if (!remove && (now - entry->GetLastUse() > maxTimeIdleSecs_)) {
            LogDebug("Deleting inactive entry: {}", entry->GetPath());
            remove = true;
        }

        if (remove) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void FileCache::EvictLru(int skipFlags) {
    if (cache_.empty()) return;
    
    // Iterate from back (least recently used) to front
    for (auto it = cache_.end(); it != cache_.begin(); ) {
        --it;
        if ((*it)->IsFlagSet(skipFlags)) continue;

        LogNotice("Deleting least-recently-used FILE entry: cmd=\"{}\"", (*it)->GetPath().c_str());
        cache_.erase(it);
        return;
    }
}

void FileCache::RemoveAndFree(IFileEntry* entry) {
    if (!entry) return;
    auto it = std::find_if(cache_.begin(), cache_.end(), 
                           [entry](const auto& e) { return e.get() == entry; });
    if (it != cache_.end()) {
        cache_.erase(it);
    }
}

} // namespace pqact
} 
