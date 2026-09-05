#pragma once
#include <pthread.h>
#include <cstdint>
#include <unistd.h>
#include <type_traits>

namespace rdm {

// Must remain a standard-layout POD type to safely embed in memory-mapped files.
struct SharedSyncState {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    uint64_t        version;
};
static_assert(std::is_standard_layout_v<SharedSyncState>, "SharedSyncState must be standard-layout");

class ProcessNotifier {
private:
    SharedSyncState* state_;
    bool isReadOnly_;

public:
    ProcessNotifier(SharedSyncState* state, bool isReadOnly) 
        : state_(state), isReadOnly_(isReadOnly) {}

    // Called once during product queue creation
    static int Initialize(SharedSyncState* state);

    // Called by writers to announce new data
    int Notify();

    // Called by readers. Handles EOWNERDEAD recovery and read-only polling.
    int Wait(uint64_t lastSeenVersion, unsigned int timeoutSecs);

    uint64_t GetVersion() const;
};

} // namespace rdm
