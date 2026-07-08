// storage/pq/PqLocks.cpp
#include "PqLocks.h"
#include "ProductQueue.h"
#include "Log.h"
#include "BitUtil.h"
#include "RegionManager.h"
#include "QueueIndexManager.h"
#include <cstdlib>
#include <cstring>
#include <cerrno>

namespace rdm {

PqMutex::PqMutex(bool isThreadSafe) : isThreadSafe_(isThreadSafe) {
    if (isThreadSafe_) {
        pthread_mutexattr_t attr;
        if (pthread_mutexattr_init(&attr) == 0) {
            // Enable multi-process synchronization across the shared-memory boundary
            (void)pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            
            (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
            (void)pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
            int status = pthread_mutex_init(&mutex_, &attr);
            (void)pthread_mutexattr_destroy(&attr);
            if (status) {
                LogFatal("Failed to initialize PqMutex: {}", std::strerror(status));
                std::abort();
            }
        } else {
            LogFatal("Failed to initialize PqMutex attributes");
            std::abort();
        }
    }
}

PqMutex::~PqMutex() {
    if (isThreadSafe_) {
        pthread_mutex_destroy(&mutex_);
    }
}

void PqMutex::lock(int& savedCancelState) {
    if (isThreadSafe_) {
        (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &savedCancelState);
        int status = pthread_mutex_lock(&mutex_);
        if (status) {
            LogError("pthread_mutex_lock() failure: {}", std::strerror(status));
            std::abort();
        }
    }
}

void PqMutex::unlock(int savedCancelState) {
    if (isThreadSafe_) {
        int status = pthread_mutex_unlock(&mutex_);
        if (status) {
            LogError("pthread_mutex_unlock() failure: {}", std::strerror(status));
            std::abort();
        }
        int dummy;
        (void)pthread_setcancelstate(savedCancelState, &dummy);
    }
}

ThreadLock::ThreadLock(PqMutex& mtx) : mutex_(mtx) {
    mutex_.lock(previousCancelState_);
}

ThreadLock::~ThreadLock() {
    mutex_.unlock(previousCancelState_);
}

// ==============================================================================
// ControlLock: Pure RAII Context Manager
// ==============================================================================

ControlLock::ControlLock(ProductQueue& pq, int rflags)
    : pq_(pq), rflags_(rflags), status_(0) {

    // 1. Mask fatal signals securely before modifying the file
    if ((rflags_ & RegionFlags::Write) != 0) {
        sigset_t set;
        (void) sigfillset(&set);
        (void) sigdelset(&set, SIGABRT);
        (void) sigdelset(&set, SIGFPE);
        (void) sigdelset(&set, SIGILL);
        (void) sigdelset(&set, SIGSEGV);
        (void) sigdelset(&set, SIGBUS);

        if (pthread_sigmask(SIG_BLOCK, &set, &sav_set_) == 0) {
            signals_blocked_ = true;
        }
    }

    // 2. Map the Control Region
    if (!pq_.ctlRegion_) {
        pq_.ctlRegion_ = pq_.regionManager_->getRegion(0, static_cast<size_t>(pq_.dataOffset_), rflags_);
        if (!pq_.ctlRegion_) {
            status_ = EIO;
            return;
        }
    }

    pqctl* tempCtl = static_cast<pqctl*>(pq_.ctlRegion_->get());

    // 3. Map the Index Region
    if (!pq_.idxRegion_) {
        pq_.idxRegion_ = pq_.regionManager_->getRegion(pq_.indexOffset_, tempCtl->ixsz, rflags_ | RegionFlags::NoLock);
        if (!pq_.idxRegion_) {
            status_ = EIO;
            return;
        }
    }

    // 4. Layout the structural schemas over the mapped memory
    if (pq_.indexManager_.LayoutExisting(tempCtl, pq_.idxRegion_->get(), pq_.indexSize_, pq_.maxSlots_, tempCtl->align) != 0) {
        status_ = EIO;
        return;
    }
}

ControlLock::~ControlLock() {
    // 1. Determine if we need to flush modifications
    int releaseFlags = (status_ == 0) ? (rflags_ & RegionFlags::Modified) : 0;

    // 2. Unmap the Index Region
    if (pq_.idxRegion_) {
        if (fIsSet(releaseFlags, RegionFlags::Modified)) {
            pq_.idxRegion_->markModified();
        }
        pq_.idxRegion_.reset();
    }

    // 3. Unmap the Control Region
    if (pq_.ctlRegion_) {
        if (fIsSet(releaseFlags, RegionFlags::Modified)) {
            pq_.ctlRegion_->markModified();
        }
        pq_.ctlRegion_.reset();
    }

    // 4. Detach overlay pointers
    pq_.indexManager_.Clear();

    // 5. Restore the thread's signal mask
    if (signals_blocked_) {
        (void) pthread_sigmask(SIG_SETMASK, &sav_set_, nullptr);
    }
}

}
