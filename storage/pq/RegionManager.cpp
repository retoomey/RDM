#include "RegionManager.h"
#include "MappedRegion.h"
#include "FileUtil.h"
#include "BitUtil.h"
#include "Log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace rdm {

class RegionMapper {
public:
    virtual ~RegionMapper() = default;
    virtual int fetch(rdm::RegionManager* mgr, off_t offset, size_t extent, int rflags, void** ptrp) = 0;
    virtual int commit(rdm::RegionManager* mgr, off_t offset, size_t extent, int rflags, void* vp) = 0;
};

class FileRegionMapper : public RegionMapper {
public:
    int fetch(rdm::RegionManager* mgr, off_t offset, size_t extent, int rflags, void** ptrp) override {
        // std::make_unique for an array value-initializes (zeroes out) the memory, 
        // perfectly replicating the behavior of std::calloc while providing exception safety.
        std::unique_ptr<uint8_t[]> buffer;
        try {
            buffer = std::make_unique<uint8_t[]>(extent);
        } catch (const std::bad_alloc&) {
            LogSyserr("FileRegionMapper: Couldn't allocate {} bytes", static_cast<unsigned long>(extent));
            return ENOMEM;
        }

        ssize_t nread = pread(mgr->getFd(), buffer.get(), extent, offset);
        if (nread == -1) {
            int err = errno;
            LogSyserr("FileRegionMapper: Read failure at offset {}", static_cast<long>(offset));
            return err; // buffer is automatically destroyed
        } else if (nread > 0 && static_cast<size_t>(nread) != extent) {
            LogError("FileRegionMapper: Short read anomaly at offset {}", static_cast<long>(offset));
            return EIO; // buffer is automatically destroyed
        }

        // Surrender ownership to the caller (MappedRegion) so it conforms to the raw void* interface.
        *ptrp = buffer.release();
        return 0;
    }

    int commit(rdm::RegionManager* mgr, off_t offset, size_t extent, int rflags, void* vp) override {
        // Immediately wrap the raw pointer back into a unique_ptr. 
        // This guarantees destruction via delete[] when commit() exits, regardless of early returns.
        std::unique_ptr<uint8_t[]> buffer(static_cast<uint8_t*>(vp));
        int status = 0;

        if (rdm::fIsSet(rflags, rdm::RegionFlags::Modified)) {
            ssize_t nwrote = pwrite(mgr->getFd(), buffer.get(), extent, offset);
            if (nwrote == -1) {
                LogSyserr("FileRegionMapper: Write failure at offset {}", static_cast<long>(offset));
                status = errno;
            } else if (static_cast<size_t>(nwrote) != extent) {
                LogError("FileRegionMapper: Short write layout variance at offset {}", static_cast<long>(offset));
                status = EIO;
            }
        }

        return status; // buffer goes out of scope and is safely destroyed
    }
};

class ChunkedMmapMapper : public RegionMapper {
public:
    int fetch(rdm::RegionManager* mgr, off_t offset, size_t extent, int rflags, void** ptrp) override {
        int mflags = fIsSet(mgr->getFlags(), rdm::PqFlags::Private) ? MAP_PRIVATE : MAP_SHARED;
        int prot = (rdm::fIsSet(mgr->getFlags(), rdm::PqFlags::ReadOnly) && !fIsSet(rflags, rdm::RegionFlags::Write)) 
                   ? PROT_READ : (PROT_READ | PROT_WRITE);
        
        size_t rem = offset % mgr->getPageSize();
        size_t pagext = roundUp(rem + extent, mgr->getPageSize());
        off_t pageo = offset - rem;

        if (fIsSet(prot, PROT_WRITE)) {
            int status = fgrow(mgr->getFd(), offset + extent, fIsSet(mgr->getFlags(), rdm::PqFlags::Sparse));
            if (status) {
                LogError("ChunkedMmapMapper: fgrow() failure {}", std::strerror(status));
                return status;
            }
        }

        void* vp = nullptr;
        int status = mapwrap(mgr->getFd(), pageo, pagext, prot, mflags, &vp);
        if (status == 0) {
            *ptrp = static_cast<char*>(vp) + rem;
        }
        return status;
    }

    int commit(rdm::RegionManager* mgr, off_t offset, size_t extent, int rflags, void* vp) override {
        off_t rem = offset % static_cast<off_t>(mgr->getPageSize());
        void* base = static_cast<char*>(vp) - rem;
        size_t pagext = roundUp(rem + extent, static_cast<off_t>(mgr->getPageSize()));
        
        int status = unmapwrap(base, offset - rem, pagext, 0);
        if (status) {
            LogError("ChunkedMmapMapper: unmapwrap() kernel synchronization failure {}", std::strerror(status));
        }
        return status;
    }
};

class FlatMmapMapper : public RegionMapper {
private:
    void* base_{nullptr};
    off_t total_size_{0};

    int initFlatMap(rdm::RegionManager* mgr) {
        struct stat sb;
        if (fstat(mgr->getFd(), &sb) < 0) return errno;
        total_size_ = sb.st_size;

        int mflags = rdm::fIsSet(mgr->getFlags(), rdm::PqFlags::Private) ? MAP_PRIVATE : MAP_SHARED;
        int prot = rdm::fIsSet(mgr->getFlags(), rdm::PqFlags::ReadOnly) ? PROT_READ : (PROT_READ | PROT_WRITE);

        int status = mapwrap(mgr->getFd(), 0, total_size_, prot, mflags, &base_);
        return status;
    }

public:
    ~FlatMmapMapper() override {
        if (base_) unmapwrap(base_, 0, total_size_, 0);
    }

    int fetch(rdm::RegionManager* mgr, off_t offset, size_t extent, int rflags, void** ptrp) override {
        if (!base_) {
            int status = initFlatMap(mgr);
            if (status != 0) return status;
        }
        *ptrp = static_cast<char*>(base_) + offset;
        return 0;
    }

    int commit(rdm::RegionManager* mgr, off_t offset, size_t extent, int rflags, void* vp) override {
        return 0; // Flat mapping persists for the life of the manager
    }
};

MappedRegion::MappedRegion(MappedRegion&& other) noexcept
    : manager_(other.manager_), offset_(other.offset_), extent_(other.extent_), rflags_(other.rflags_), ptr_(other.ptr_) {
    other.ptr_ = nullptr;
    other.manager_ = nullptr;
}

MappedRegion& MappedRegion::operator=(MappedRegion&& other) noexcept {
    if (this != &other) {
        if (ptr_ && manager_) manager_->releaseRegion(offset_, extent_, rflags_, ptr_);
        manager_ = other.manager_;
        offset_ = other.offset_;
        extent_ = other.extent_;
        rflags_ = other.rflags_;
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
        other.manager_ = nullptr;
    }
    return *this;
}

MappedRegion::~MappedRegion() {
    if (ptr_ && manager_) {
        manager_->releaseRegion(offset_, extent_, rflags_, ptr_);
    }
}

void MappedRegion::markModified() {
    rdm::fSet(rflags_, rdm::RegionFlags::Modified);
}

RegionManager::~RegionManager() = default;

RegionManager::RegionManager(int fd, int pflags, size_t pagesz)
    : fd_(fd), pflags_(pflags), pagesz_(pagesz) {
    
    // Select the internal mapping strategy based on flags
    if (fIsSet(pflags_, rdm::PqFlags::NoMap)) {
        mapperStrategy_ = std::make_unique<FileRegionMapper>();
    } else if (fIsSet(pflags_, rdm::PqFlags::MapRgns)) {
        mapperStrategy_ = std::make_unique<ChunkedMmapMapper>();
    } else {
        mapperStrategy_ = std::make_unique<FlatMmapMapper>();
    }
}

int RegionManager::lockKernelRegion(off_t offset, size_t extent, int rflags) {
    if (fIsSet(rflags, rdm::RegionFlags::NoLock) || fIsSet(pflags_, rdm::PqFlags::NoLock)) {
        return 0;
    }
    int cmd = fIsSet(rflags, rdm::RegionFlags::NoWait) ? F_SETLK : F_SETLKW;
    short l_type = fIsSet(rflags, rdm::RegionFlags::Write) ? F_WRLCK : F_RDLCK;
    return rdm::fd_lock(fd_, cmd, l_type, offset, SEEK_SET, extent);
}

int RegionManager::unlockKernelRegion(off_t offset, size_t extent, int rflags) {
    if (fIsSet(rflags, rdm::RegionFlags::NoLock) || fIsSet(pflags_, rdm::PqFlags::NoLock)) {
        return 0;
    }
    return rdm::fd_lock(fd_, F_SETLK, F_UNLCK, offset, SEEK_SET, extent);
}

std::unique_ptr<MappedRegion> RegionManager::getRegion(off_t offset, size_t extent, int rflags) {
    // 1. Check if region is already active
    auto it = std::lower_bound(activeRegions_.begin(), activeRegions_.end(), offset,
        [](const ActiveRegion& rgn, off_t target_offset) {
            return rgn.offset < target_offset;
        });
        
    if (it != activeRegions_.end() && it->offset == offset) {
        LogError("RegionManager: Region at offset {} is already actively locked/mapped", static_cast<long>(offset));
        return nullptr;
    }

    // 2. Lock the region in the kernel
    int status = lockKernelRegion(offset, extent, rflags);
    if (status != 0 && status != EAGAIN && status != EACCES) {
        LogError("RegionManager: Kernel lock error on offset {}", static_cast<long>(offset));
        return nullptr;
    }

    // 3. Delegate to the mapper strategy
    void* ptr = nullptr;
    status = mapperStrategy_->fetch(this, offset, extent, rflags, &ptr);
    if (status != 0) {
        unlockKernelRegion(offset, extent, rflags);
        return nullptr;
    }

    // 4. Track it and return the RAII object
    ActiveRegion newRgn{offset, extent, ptr, rflags};
    activeRegions_.insert(it, newRgn);

    return std::make_unique<MappedRegion>(this, offset, extent, rflags, ptr);
}

void RegionManager::releaseRegion(off_t offset, size_t extent, int rflags, void* ptr) {
    // 1. Remove from active tracker
    auto it = std::lower_bound(activeRegions_.begin(), activeRegions_.end(), offset,
        [](const ActiveRegion& rgn, off_t target_offset) {
            return rgn.offset < target_offset;
        });

    if (it == activeRegions_.end() || it->offset != offset) {
        LogError("RegionManager: Cannot release untracked offset {}", static_cast<long>(offset));
        return;
    }

    activeRegions_.erase(it);

    // 2. Unmap / Free via strategy
    mapperStrategy_->commit(this, offset, extent, rflags, ptr);

    // 3. Unlock the kernel region
    unlockKernelRegion(offset, extent, rflags);
}
}
