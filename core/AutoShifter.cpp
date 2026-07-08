#include "AutoShifter.h"
#include "Log.h"
#include <cerrno>

namespace rdm {

AutoShifter::AutoShifter(bool isPrimary, unsigned int ldmCount, double intervalSeconds) noexcept
    : ldmCount_(ldmCount),
      isPrimary_(isPrimary),
      shouldSwitch_(false),
      intervalSeconds_(intervalSeconds) {
    ResetInternal();
}

void AutoShifter::Init(bool isPrimary) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    isPrimary_ = isPrimary;
    ResetInternal();
}

void AutoShifter::ResetInternal() noexcept {
    prevCompTime_ = std::chrono::steady_clock::now();
    shouldSwitch_ = false;
    queue_.clear();
}

int AutoShifter::SetLdmCount(unsigned int count) noexcept {
    if (count == 0) return EINVAL;
    
    std::lock_guard<std::mutex> lock(mutex_);
    if (count != ldmCount_) {
        ResetInternal();
        ldmCount_ = count;
    }
    return 0;
}

int AutoShifter::Process(int success, size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (ldmCount_ <= 1) return 0;

    auto now = std::chrono::steady_clock::now();
    try {
        queue_.push_back({now, success});
    } catch (const std::bad_alloc&) {
        return ENOMEM;
    }

    // Drop tracking history elements older than our evaluation checkpoint boundary
    while (!queue_.empty() && queue_.front().time <= prevCompTime_) {
        queue_.pop_front();
    }

    std::chrono::duration<double> elapsed = now - prevCompTime_;
    const double period = elapsed.count();

    // Strict replication of the legacy timing calculation triggers
    if (period < (2.0 * intervalSeconds_)) {
        shouldSwitch_ = false;
        LogDebug("AutoShifter: period={} s (skipping evaluation)", period);
    } else {
        unsigned int acceptedCount = 0;
        unsigned int rejectedCount = 0;

        for (const auto& entry : queue_) {
            if (entry.wasAccepted) ++acceptedCount;
            else ++rejectedCount;
        }

        if (acceptedCount + rejectedCount == 0) {
            shouldSwitch_ = false;
            LogDebug("AutoShifter: period={} s, #accept=0, #reject=0", period);
        } else {
            const double rejectedMean = static_cast<double>(rejectedCount) / (ldmCount_ - 1);
            shouldSwitch_ = isPrimary_ ? (static_cast<double>(acceptedCount) <= rejectedMean) 
                                       : (static_cast<double>(acceptedCount) >= rejectedMean);
            
            LogDebug("AutoShifter: period={} s, #accept={}, #reject={}, #LDM-s={}, primary={}, switch={}",
                      period, acceptedCount, rejectedCount, ldmCount_, isPrimary_, shouldSwitch_);
        }
        prevCompTime_ = now;
    }

    return 0;
}

bool AutoShifter::ShouldSwitch() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return (ldmCount_ == 1) ? !isPrimary_ : shouldSwitch_;
}

}
