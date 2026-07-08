#include "PqFile.h"
#include "ProcessUtil.h"
#include "BitUtil.h" 
#include "IProductStore.h" 

#include <cerrno>

#include <fcntl.h>
#include <unistd.h>

namespace rdm {

PqFile::PqFile() {
    long sz = sysconf(_SC_PAGESIZE);
    pagesz_ = (sz > 0) ? static_cast<size_t>(sz) : 4096;
}

PqFile::~PqFile() {
    close();
}

int PqFile::open(const std::string& path, bool readOnly) {
    close();
    int flags = readOnly ? O_RDONLY : O_RDWR;
    fd_ = ::open(path.c_str(), flags, 0);
    if (fd_ < 0) {
        return errno;
    }
    
    os::ensureCloseOnExec(fd_);
    pathname_ = path;
    return 0;
}

int PqFile::create(const std::string& path, mode_t mode, int pflags) {
    close();
    int oflags = O_RDWR | O_CREAT | O_TRUNC;
    if (fIsSet(pflags, PqFlags::NoClobber)) {
        fSet(oflags, O_EXCL);
    }
    fd_ = ::open(path.c_str(), oflags, mode);
    if (fd_ < 0) return errno;
    
    os::ensureCloseOnExec(fd_);
    pathname_ = path;
    return 0;
}

int PqFile::close() {
    int status = 0;
    if (fd_ >= 0) {
        if (::close(fd_) < 0) {
            status = errno;
        }
        fd_ = -1;
    }
    return status;
}

int PqFile::unlink() {
    close();
    if (!pathname_.empty() && ::unlink(pathname_.c_str()) < 0) {
        return errno;
    }
    return 0;
}

}
