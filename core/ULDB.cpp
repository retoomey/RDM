#include "ULDB.h"
#include "config.h"
#include "Log.h"
#include "Registry.h"
#include "NetworkUtils.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <array>

namespace rdm {

namespace {
    constexpr size_t MAX_SPECS_PER_ENTRY = 32;
    constexpr size_t MAX_PATTERN_LEN = 256;

    struct ShmProdSpec {
        FeedType feedtype;
        char pattern[MAX_PATTERN_LEN];
    };

    struct ShmProdClass {
        int64_t from_sec;
        int32_t from_usec;
        int64_t to_sec;
        int32_t to_usec;
        size_t numSpecs;
        ShmProdSpec specs[MAX_SPECS_PER_ENTRY];
    };

    struct ShmEntry {
        bool isActive;
        pid_t pid;
        int protoVers;
        int isNotifier;
        int isPrimary;
        struct sockaddr_storage sockAddr;
        ShmProdClass prodClass;
    };

    struct ShmHeader {
        size_t maxEntries;
        size_t activeEntries;
    };

    ShmEntry* GetEntriesArray(void* mappedRegion) {
        return reinterpret_cast<ShmEntry*>(static_cast<char*>(mappedRegion) + sizeof(ShmHeader));
    }
} // anonymous namespace

Uldb::~Uldb() {
    Close();
}

std::string Uldb::GetPosixShmName(const std::string& path) const {
    std::string searchPath = path.empty() ? registry::getQueuePath() : path;
    std::string name = "/ldm_uldb";
    for (char c : searchPath) {
        name += (c == '/') ? '_' : c;
    }
    return name;
}

UldbStatus Uldb::AttachSharedMemory() {
    shmFd_ = shm_open(shmName_.c_str(), O_RDWR, 0666);
    if (shmFd_ == -1) {
        if (errno == ENOENT) return UldbStatus::EXIST;
        LogSyserr("Failed to open shared memory segment: {}", shmName_);
        return UldbStatus::SYSTEM;
    }

    struct stat sb;
    if (fstat(shmFd_, &sb) == -1) {
        LogSyserr("fstat failed on shared memory {}", shmName_);
        close(shmFd_);
        shmFd_ = -1;
        return UldbStatus::SYSTEM;
    }

    mappedSize_ = sb.st_size;
    mappedRegion_ = mmap(nullptr, mappedSize_, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd_, 0);
    
    if (mappedRegion_ == MAP_FAILED) {
        LogSyserr("mmap failed for shared-memory segment {}", shmName_);
        close(shmFd_);
        shmFd_ = -1;
        mappedRegion_ = nullptr;
        return UldbStatus::SYSTEM;
    }
    return UldbStatus::SUCCESS;
}

void Uldb::DetachSharedMemory() {
    if (mappedRegion_ && mappedRegion_ != MAP_FAILED) {
        munmap(mappedRegion_, mappedSize_);
        mappedRegion_ = nullptr;
        mappedSize_ = 0;
    }
    if (shmFd_ >= 0) {
        close(shmFd_);
        shmFd_ = -1;
    }
}

UldbStatus Uldb::Create(const std::string& path, unsigned capacity) {
    shmName_ = GetPosixShmName(path);
    
    size_t maxEntries = capacity / sizeof(ShmEntry);
    if (maxEntries == 0) maxEntries = 100; 
    size_t requiredBytes = sizeof(ShmHeader) + (maxEntries * sizeof(ShmEntry));

    // Ensure we unlink any stale segment clean before formatting a new one
    shm_unlink(shmName_.c_str());

    int fd = shm_open(shmName_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0666);
    if (fd == -1) {
        LogSyserr("shm_open(O_CREAT|O_EXCL) failure: name={}", shmName_);
        return UldbStatus::SYSTEM;
    }

    if (ftruncate(fd, requiredBytes) == -1) {
        LogSyserr("ftruncate failed on {}", shmName_);
        close(fd);
        shm_unlink(shmName_.c_str());
        return UldbStatus::SYSTEM;
    }

    mappedSize_ = requiredBytes;
    mappedRegion_ = mmap(nullptr, mappedSize_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (mappedRegion_ == MAP_FAILED) {
        LogSyserr("mmap failed on {}", shmName_);
        mappedRegion_ = nullptr;
        mappedSize_ = 0;
        shm_unlink(shmName_.c_str());
        return UldbStatus::SYSTEM;
    }

    // Initialize Header
    ShmHeader* header = static_cast<ShmHeader*>(mappedRegion_);
    header->maxEntries = maxEntries;
    header->activeEntries = 0;

    // Initialize all entries to inactive
    ShmEntry* entries = GetEntriesArray(mappedRegion_);
    for (size_t i = 0; i < maxEntries; ++i) {
        entries[i].isActive = false;
    }

    // [FIX] Do NOT munmap here. Retain the active mapping pool so that
    // parent daemons and inherited child processes have instant access.

    if (lock_.Create(shmName_) != 0) {
        LogSyserr("Couldn't create lock component");
        munmap(mappedRegion_, mappedSize_);
        mappedRegion_ = nullptr;
        mappedSize_ = 0;
        shm_unlink(shmName_.c_str());
        return UldbStatus::SYSTEM;
    }

    return UldbStatus::SUCCESS;
}

UldbStatus Uldb::Open(const std::string& path) {
    shmName_ = GetPosixShmName(path);
    if (mappedRegion_) {
        // Already mapped by Create() call in the same process footprint
        return UldbStatus::SUCCESS;
    }
    UldbStatus status = AttachSharedMemory();
    if (status == UldbStatus::SUCCESS) {
        if (lock_.Attach(shmName_) != 0) {
            LogSyserr("Couldn't attach to existing lock component");
            DetachSharedMemory();
            return UldbStatus::SYSTEM;
        }
    }
    return status;
}

UldbStatus Uldb::Close() {
    DetachSharedMemory();
    return UldbStatus::SUCCESS;
}

UldbStatus Uldb::Delete(const std::string& path) {
    std::string ldm_shm_name = GetPosixShmName(path);
    UldbStatus status = UldbStatus::SUCCESS;

    DetachSharedMemory();

    if (shm_unlink(ldm_shm_name.c_str()) == -1) {
        if (errno != ENOENT) {
            LogSyserr("shm_unlink failed for {}", ldm_shm_name);
            status = UldbStatus::SYSTEM;
        } else {
            status = UldbStatus::EXIST;
        }
    }

    if (SemaphoreRWLock::DeleteByName(ldm_shm_name) != 0 && errno != ENOENT) {
        LogSyserr("Couldn't delete existing semaphore lock");
        status = UldbStatus::SYSTEM;
    }

    return status;
}

UldbStatus Uldb::GetSize(unsigned& size) {
    ScopedReadLock guard(lock_);
    if (!guard.IsLocked() || !mappedRegion_) return UldbStatus::SYSTEM;

    ShmHeader* header = static_cast<ShmHeader*>(mappedRegion_);
    size = header->activeEntries;
    return UldbStatus::SUCCESS;
}

UldbStatus Uldb::AddProcess(pid_t pid, int protoVers, const struct sockaddr_storage* sockAddr,
                            const ProdClass& desired, ProdClass& allowed,
                            int isNotifier, int isPrimary) {
    if (pid <= 0) return UldbStatus::ARG;

    ScopedWriteLock guard(lock_);
    if (!guard.IsLocked() || !mappedRegion_) return UldbStatus::SYSTEM;

    ShmHeader* header = static_cast<ShmHeader*>(mappedRegion_);
    ShmEntry* entries = GetEntriesArray(mappedRegion_);

    allowed = desired;

    if (registry::isAntiDosEnabled()) {
        for (size_t i = 0; i < header->maxEntries; ++i) {
            if (!entries[i].isActive) continue;

            if (pid == entries[i].pid) {
                LogWarning("Entry already exists for PID {}", static_cast<long>(pid));
                return UldbStatus::EXIST;
            }

            if (protoVers == entries[i].protoVers && 
                network::IpAddressesAreEqual(sockAddr, &entries[i].sockAddr) && 
                !isNotifier && !entries[i].isNotifier) {
                
                if (kill(entries[i].pid, SIGTERM) == 0) {
                    LogNotice("Terminated redundant upstream LDM (PID: {})", entries[i].pid);
                } else {
                    LogWarning("Couldn't terminate redundant upstream LDM (PID: {})", entries[i].pid);
                }
                
                entries[i].isActive = false;
                header->activeEntries--;
            }
        }
    }

    if (allowed.specs.empty()) return UldbStatus::SUCCESS;

    ShmEntry* newEntry = nullptr;
    for (size_t i = 0; i < header->maxEntries; ++i) {
        if (!entries[i].isActive) {
            newEntry = &entries[i];
            break;
        }
    }

    if (!newEntry) {
        LogSyserr("ULDB is at maximum capacity ({} entries). Cannot add new upstream connection.", header->maxEntries);
        return UldbStatus::SYSTEM;
    }

    newEntry->isActive = true;
    newEntry->pid = pid;
    newEntry->protoVers = protoVers;
    newEntry->isNotifier = isNotifier;
    newEntry->isPrimary = isPrimary;
    std::memcpy(&newEntry->sockAddr, sockAddr, sizeof(struct sockaddr_storage));

    newEntry->prodClass.from_sec = allowed.from_sec;
    newEntry->prodClass.from_usec = allowed.from_usec;
    newEntry->prodClass.to_sec = allowed.to_sec;
    newEntry->prodClass.to_usec = allowed.to_usec;
    
    size_t specCount = std::min(allowed.specs.size(), MAX_SPECS_PER_ENTRY);
    newEntry->prodClass.numSpecs = specCount;

    for (size_t i = 0; i < specCount; ++i) {
        newEntry->prodClass.specs[i].feedtype = allowed.specs[i].feedtype;
        std::strncpy(newEntry->prodClass.specs[i].pattern, allowed.specs[i].pattern.c_str(), MAX_PATTERN_LEN - 1);
        newEntry->prodClass.specs[i].pattern[MAX_PATTERN_LEN - 1] = '\0';
    }

    header->activeEntries++;
    return UldbStatus::SUCCESS;
}

UldbStatus Uldb::Remove(pid_t pid) {
    if (pid <= 0) return UldbStatus::ARG;

    ScopedWriteLock guard(lock_);
    if (!guard.IsLocked() || !mappedRegion_) return UldbStatus::SYSTEM;

    ShmHeader* header = static_cast<ShmHeader*>(mappedRegion_);
    ShmEntry* entries = GetEntriesArray(mappedRegion_);

    for (size_t i = 0; i < header->maxEntries; ++i) {
        if (entries[i].isActive && entries[i].pid == pid) {
            entries[i].isActive = false;
            header->activeEntries--;
            return UldbStatus::SUCCESS;
        }
    }

    LogWarning("Entry for PID {} not found", pid);
    return UldbStatus::EXIST;
}

UldbStatus Uldb::GetEntries(std::vector<UldbEntry>& outEntries) {
    outEntries.clear();
    
    ScopedReadLock guard(lock_);
    if (!guard.IsLocked() || !mappedRegion_) return UldbStatus::SYSTEM;

    ShmHeader* header = static_cast<ShmHeader*>(mappedRegion_);
    ShmEntry* entries = GetEntriesArray(mappedRegion_);

    for (size_t i = 0; i < header->maxEntries; ++i) {
        if (!entries[i].isActive) continue;

        UldbEntry out;
        out.pid = entries[i].pid;
        out.protoVers = entries[i].protoVers;
        out.isNotifier = entries[i].isNotifier;
        out.isPrimary = entries[i].isPrimary;
        std::memcpy(&out.sockAddr, &entries[i].sockAddr, sizeof(struct sockaddr_storage));

        out.prodClass.from_sec = entries[i].prodClass.from_sec;
        out.prodClass.from_usec = entries[i].prodClass.from_usec;
        out.prodClass.to_sec = entries[i].prodClass.to_sec;
        out.prodClass.to_usec = entries[i].prodClass.to_usec;

        for (size_t j = 0; j < entries[i].prodClass.numSpecs; ++j) {
            ProdSpec spec;
            spec.feedtype = entries[i].prodClass.specs[j].feedtype;
            spec.pattern = entries[i].prodClass.specs[j].pattern;
            out.prodClass.specs.push_back(spec);
        }

        outEntries.push_back(std::move(out));
    }

    return UldbStatus::SUCCESS;
}

UldbStatus Uldb::ReadLock()  { return lock_.LockRead() == 0 ? UldbStatus::SUCCESS : UldbStatus::SYSTEM; }
UldbStatus Uldb::WriteLock() { return lock_.LockWrite() == 0 ? UldbStatus::SUCCESS : UldbStatus::SYSTEM; }
UldbStatus Uldb::Unlock()    { return lock_.Unlock() == 0 ? UldbStatus::SUCCESS : UldbStatus::SYSTEM; }

} // namespace rdm
