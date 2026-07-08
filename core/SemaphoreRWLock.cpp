#include "SemaphoreRWLock.h"
#include "Log.h"
#include "Registry.h"
#include "FileUtil.h"

#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace rdm {

namespace {
    std::string GetLockFilePath(const std::string& name) {
        std::string safeName = name;
        for (char& c : safeName) {
            if (c == '/') c = '_';
        }
        
        // Fetch the absolute run directory from the registry
        std::string runDir = registry::getLdmVarRunDir();
        
        // Ensure the directory exists with proper permissions
        if (EnsureDirectoryAccess(runDir, true) != 0) {
            LogSyserr("Failed to access or create lock directory: {}", runDir);
        }

        return runDir + "/ldm_rwlock_" + safeName + ".lck";
    }
}

SemaphoreRWLock::~SemaphoreRWLock() {
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}

int SemaphoreRWLock::Create(const std::string& name) {
    if (fd_ != -1) close(fd_);
    
    lockFilePath_ = GetLockFilePath(name);
    
    // Create or open the lock file
    fd_ = open(lockFilePath_.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ == -1) {
        LogSyserr("Couldn't create POSIX lock file: {}", lockFilePath_);
        return errno;
    }
    return 0;
}

int SemaphoreRWLock::Attach(const std::string& name) {
    if (fd_ != -1) close(fd_);
    
    lockFilePath_ = GetLockFilePath(name);
    fd_ = open(lockFilePath_.c_str(), O_RDWR);
    if (fd_ == -1) {
        LogSyserr("Couldn't attach to existing POSIX lock file: {}", lockFilePath_);
        return errno;
    }
    return 0;
}

int SemaphoreRWLock::DestroyOSResource() {
    if (!IsValid()) return 0;
    
    if (close(fd_) == -1) {
        LogSyserr("Couldn't close lock file fd: {}", fd_);
    }
    fd_ = -1;
    numReadLocks_ = 0;
    numWriteLocks_ = 0;

    if (unlink(lockFilePath_.c_str()) == -1 && errno != ENOENT) {
        LogSyserr("Couldn't delete lock file: {}", lockFilePath_);
        return errno;
    }
    return 0;
}

int SemaphoreRWLock::DeleteByName(const std::string& name) {
    std::string path = GetLockFilePath(name);
    if (unlink(path.c_str()) == -1 && errno != ENOENT) {
        LogSyserr("Couldn't delete existing lock file {}", path);
        return errno;
    }
    return 0;
}

int SemaphoreRWLock::PerformLock(short lockType) {
    struct flock fl;
    fl.l_type = lockType;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0; // 0 means lock the whole file

    int status;
    do {
        // F_SETLKW blocks until the lock is acquired. 
        // We gracefully retry if interrupted by a signal (like a heartbeat timer).
        status = fcntl(fd_, F_SETLKW, &fl);
    } while (status == -1 && errno == EINTR);

    if (status == -1) {
        LogSyserr("fcntl lock failure on {}", lockFilePath_);
        return errno;
    }
    return 0;
}

int SemaphoreRWLock::LockRead() {
    if (!IsValid()) return EINVAL;

    if (numWriteLocks_ > 0) {
        LogError("Lock is already locked for writing in this process.");
        return EDEADLK;
    }

    if (numReadLocks_ > 0) {
        numReadLocks_++;
        return 0;
    }
    
    int status = PerformLock(F_RDLCK);
    if (status == 0) numReadLocks_ = 1;
    return status;
}

int SemaphoreRWLock::LockWrite() {
    if (!IsValid()) return EINVAL;

    if (numReadLocks_ > 0) {
        LogError("Lock is already locked for reading in this process.");
        return EDEADLK;
    }

    if (numWriteLocks_ > 0) {
        numWriteLocks_++;
        return 0;
    }
    
    int status = PerformLock(F_WRLCK);
    if (status == 0) numWriteLocks_ = 1;
    return status;
}

int SemaphoreRWLock::Unlock() {
    if (!IsValid()) return EINVAL;

    if (numWriteLocks_ > 1) {
        numWriteLocks_--;
        return 0;
    } else if (numWriteLocks_ == 1) {
        numWriteLocks_--;
    } else if (numReadLocks_ > 1) {
        numReadLocks_--;
        return 0;
    } else if (numReadLocks_ == 1) {
        numReadLocks_--;
    } else {
        // Already unlocked, no-op
        return 0;
    }

    return PerformLock(F_UNLCK);
}

// ==============================================================================
// RAII Guard Implementations
// ==============================================================================

ScopedReadLock::ScopedReadLock(SemaphoreRWLock& lock) : lock_(lock), locked_(false) {
    if (lock_.LockRead() == 0) locked_ = true;
}

ScopedReadLock::~ScopedReadLock() {
    if (locked_) lock_.Unlock();
}

ScopedWriteLock::ScopedWriteLock(SemaphoreRWLock& lock) : lock_(lock), locked_(false) {
    if (lock_.LockWrite() == 0) locked_ = true;
}

ScopedWriteLock::~ScopedWriteLock() {
    if (locked_) lock_.Unlock();
}

}
