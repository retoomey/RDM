#include "ProcessNotifier.h"
#include "Log.h"
#include "SignalManager.h"
#include <cerrno>
#include <cstring>
#include <chrono>
#include <ctime>

namespace rdm {

int ProcessNotifier::Initialize(SharedSyncState* state) {
    if (!state) return EINVAL;

    // 1. Initialize Robust Process-Shared Mutex
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mattr, PTHREAD_MUTEX_ROBUST);
    
    int status = pthread_mutex_init(&state->mutex, &mattr);
    pthread_mutexattr_destroy(&mattr);
    if (status != 0) {
        LogError("Failed to initialize robust mutex: {}", std::strerror(status));
        return status;
    }

    // 2. Initialize Monotonic Process-Shared Condition Variable
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setclock(&cattr, CLOCK_MONOTONIC);
    
    status = pthread_cond_init(&state->cond, &cattr);
    pthread_condattr_destroy(&cattr);
    if (status != 0) {
        LogError("Failed to initialize condition variable: {}", std::strerror(status));
        return status;
    }

    state->version = 0;
    return 0;
}

int ProcessNotifier::Notify() {
    if (!state_) return EINVAL;
    
    int status = pthread_mutex_lock(&state_->mutex);
    if (status == EOWNERDEAD) {
        LogNotice("ProcessNotifier recovering from abandoned mutex.");
        status = pthread_mutex_consistent(&state_->mutex);
        if (status != 0) {
            LogError("Failed to make mutex consistent: {}", std::strerror(status));
            pthread_mutex_unlock(&state_->mutex);
            return status;
        }
    } else if (status != 0) {
        LogError("Failed to lock mutex for notification: {}", std::strerror(status));
        return status;
    }

    state_->version++;
    pthread_cond_broadcast(&state_->cond);
    pthread_mutex_unlock(&state_->mutex);
    
    return 0;
}

uint64_t ProcessNotifier::GetVersion() const {
    if (!state_) return 0;
    
    // Enforce memory barrier to guarantee cross-process visibility
    if (!isReadOnly_) {
        pthread_mutex_lock(&state_->mutex);
        uint64_t v = state_->version;
        pthread_mutex_unlock(&state_->mutex);
        return v;
    }
    
    return state_->version;
}

int ProcessNotifier::Wait(uint64_t lastSeenVersion, unsigned int timeoutSecs) {
    if (!state_) return EINVAL;

    if (isReadOnly_) {
        auto start = std::chrono::steady_clock::now();
        while (state_->version == lastSeenVersion) {
            if (SignalManager::IsDone()) return EINTR;
            usleep(100000);
            if (timeoutSecs > 0) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - start).count() >= timeoutSecs) {
                    return ETIMEDOUT;
                }
            }
        }
        return 0;
    }

    int lockStatus = pthread_mutex_lock(&state_->mutex);
    if (lockStatus == EOWNERDEAD) {
        lockStatus = pthread_mutex_consistent(&state_->mutex);
        if (lockStatus != 0) {
            pthread_mutex_unlock(&state_->mutex);
            return lockStatus;
        }
    } else if (lockStatus != 0) {
        return lockStatus;
    }

    int status = 0;
    
    // Calculate the absolute deadline exactly once outside the loop
    struct timespec absolute_timeout;
    if (timeoutSecs > 0) {
        clock_gettime(CLOCK_MONOTONIC, &absolute_timeout);
        absolute_timeout.tv_sec += timeoutSecs;
    }

    while (state_->version == lastSeenVersion) {
        if (SignalManager::IsDone()) {
            status = EINTR;
            break;
        }

        if (timeoutSecs == 0) {
            // For infinite waits, re-evaluate ts every 1 second to poll for Shutdown hooks
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_sec += 1;
            status = pthread_cond_timedwait(&state_->cond, &state_->mutex, &ts);
            if (status == ETIMEDOUT) {
                status = 0;
                continue;
            }
            if (status != 0) break;
        } else {
            // Wait against the static absolute deadline
            status = pthread_cond_timedwait(&state_->cond, &state_->mutex, &absolute_timeout);
            if (status != 0) break;
        }
    }
    
    pthread_mutex_unlock(&state_->mutex);
    return status;
}

} // namespace rdm
