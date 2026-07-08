#include "FileUtil.h"
#include "Log.h"
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

namespace fs = std::filesystem;

namespace rdm {

int OpenWithMkdirs(const std::string& path, int flags, mode_t mode) {
    fs::path p(path);

    if (flags & O_CREAT) {
        fs::path parent = p.parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            std::error_code ec;
            fs::create_directories(parent, ec);
            if (ec) {
                LogError("Failed to create parent directories for {}: {}", path, ec.message());
                return -1;
            }
        }
    }
    return ::open(path.c_str(), flags, mode);
}

int EnsureDirectoryAccess(const std::string& path, bool create) {
    fs::path p(path);
    fs::path parent = p.parent_path();

    if (parent.empty()) return 0;

    std::error_code ec;
    if (fs::exists(parent, ec)) {
        return ::access(parent.c_str(), X_OK) == 0 ? 0 : -1;
    }

    if (create) {
        fs::create_directories(parent, ec);
        if (!ec) return 0;
        std::string parentStr = parent.string();
        std::string errStr = ec.message();
        LogError("Failed to create directory {}: {}", parentStr, errStr);
    }
    return -1;
}

int fsStats(int fd, off_t* fs_szp, off_t* remp) {
    struct statvfs svfs;
    if (fstatvfs(fd, &svfs) == -1) {
        return errno;
    }
    if (fs_szp) *fs_szp = static_cast<off_t>(svfs.f_frsize) * svfs.f_blocks;
    if (remp)   *remp   = static_cast<off_t>(svfs.f_frsize) * svfs.f_bavail;
    return 0;
}

long pagesize() {
    static long pgsz = 0;
    if (pgsz == 0) {
        pgsz = sysconf(_SC_PAGESIZE);
        if (pgsz == -1) pgsz = 4096; // Safe fallback
    }
    return pgsz;
}

int fgrow(int fd, off_t len, int sparse) {
    struct stat st;
    if (fstat(fd, &st) == -1) return errno;
    if (st.st_size >= len) return 0;

    if (ftruncate(fd, len) == -1) return errno;

    if (!sparse) {
        // To prevent sparse files, we must force the OS to allocate disk blocks
        // by writing a byte at the end of every page.
        long pgsz = pagesize();
        off_t offset = mRoundUp(st.st_size);
        if (offset == 0) offset = pgsz - 1;

        const char zero = 0;
        for (; offset < len; offset += pgsz) {
            if (pwrite(fd, &zero, 1, offset) != 1) return errno;
        }
        if (pwrite(fd, &zero, 1, len - 1) != 1) return errno;
    }
    return 0;
}

pid_t fd_isLocked(int fd, short l_type, off_t offset, short l_whence, size_t extent) {
    struct flock lock;
    lock.l_type = l_type;
    lock.l_whence = l_whence;
    lock.l_start = offset;
    lock.l_len = static_cast<off_t>(extent);

    if (fcntl(fd, F_GETLK, &lock) == -1) {
        return -1;
    }
    return (lock.l_type == F_UNLCK) ? 0 : lock.l_pid;
}

int fd_lock(int fd, int cmd, short l_type, off_t offset, short l_whence, size_t extent) {
    struct flock lock;
    lock.l_type = l_type;
    lock.l_whence = l_whence;
    lock.l_start = offset;
    lock.l_len = static_cast<off_t>(extent);

    if (fcntl(fd, cmd, &lock) == -1) {
        return errno;
    }
    return 0;
}

int mapwrap(int fd, off_t offset, size_t extent, int prot, int mflags, void** ptrp) {
    void* ptr = mmap(nullptr, extent, prot, mflags, fd, offset);
    if (ptr == MAP_FAILED) {
        *ptrp = nullptr;
        return errno;
    }
    *ptrp = ptr;
    return 0;
}

int unmapwrap(void* ptr, off_t /*offset*/, size_t extent, int /*mflags*/) {
    if (ptr == nullptr || ptr == MAP_FAILED) return 0;
    if (munmap(ptr, extent) == -1) {
        return errno;
    }
    return 0;
}

}
