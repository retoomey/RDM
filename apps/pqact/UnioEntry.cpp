#include "UnioEntry.h"
#include "Log.h"
#include "FileUtil.h"
#include "ProcessUtil.h"
#include "FileOpsUtil.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

namespace rdm {
namespace pqact {

void UnioEntry::ParseOptions(const std::vector<std::string>& args) {
    flags_ = 0;
    for (size_t i = 0; i < args.size() - 1; ++i) {
        if (args[i] == "-overwrite") SetFlag(FL_OVERWRITE);
        else if (args[i] == "-strip") SetFlag(FL_STRIP);
        else if (args[i] == "-metadata") SetFlag(FL_METADATA);
        else if (args[i] == "-log") SetFlag(FL_LOG);
        else if (args[i] == "-edex") SetFlag(FL_EDEX);
        else if (args[i] == "-removewmo") SetFlag(FL_STRIPWMO);
        else if (args[i] == "-flush") SetFlag(FL_FLUSH);
        else if (args[i] == "-close") SetFlag(FL_CLOSE);
        else if (args[i] == "-nodata") SetFlag(FL_NODATA);
        else if (args[i] == "-transient") UnsetFlag(FL_NOTRANSIENT);
    }
}

int UnioEntry::Open(const std::vector<std::string>& args) {
    if (args.empty()) return -1;
    ParseOptions(args);
    path_ = args.back();

    int openFlags = O_WRONLY | O_CREAT;
    if (IsFlagSet(FL_OVERWRITE)) openFlags |= O_TRUNC;

    fd_ = OpenWithMkdirs(path_, openFlags, 0666);
    if (fd_ == -1) {
        LogSyserr("Couldn't open file \"{}\"", path_);
        return -1;
    }

    if (os::ensureCloseOnExec(fd_) != 0) {
        LogSyserr("ensureCloseOnExec() failure on file \"{}\"", path_);
        Close();
        return -1;
    }

    if (!(openFlags & O_TRUNC)) {
        if (lseek(fd_, 0, SEEK_END) < 0) {
            LogSyserr("lseek() failure on file \"{}\"", path_);
        }
    }

    LogDebug("Opened fd {} for {}", fd_, path_);
    return 0;
}

void UnioEntry::Close() {
    if (fd_ != -1) {
        LogDebug("Closing fd {} ({})", fd_, path_);
        if (close(fd_) == -1) {
            LogSyserr("close: {}", path_);
        }
        fd_ = -1;
    }
}

int UnioEntry::Sync(bool block) {
    if (fd_ != -1 && IsFlagSet(FL_NEEDS_SYNC)) {
        if (fsync(fd_) == 0) {
            UnsetFlag(FL_NEEDS_SYNC);
            return 0;
        }
        if (!block && errno == EAGAIN) return 0;

        if (errno != EINTR) {
            LogSyserr("Couldn't flush I/O to file \"{}\"", path_);
            UnsetFlag(FL_NEEDS_SYNC);
        }
        return errno;
    }
    return 0;
}

int UnioEntry::Write(const Product& prod, const void* data, size_t sz) {
    int status = 0;
    
    if (IsFlagSet(FL_METADATA)) {
        std::vector<uint8_t> metaBuf = FileOpsUtil::SerializeMetadata(prod.info, sz);
        if (write(fd_, metaBuf.data(), metaBuf.size()) == -1) {
            LogError("Couldn't write product metadata to file");
            status = -1;
        }
    }
    
    if (status == 0 && !IsFlagSet(FL_NODATA) && sz > 0) {
        size_t remaining = sz;
        const char* ptr = static_cast<const char*>(data);
        while (remaining > 0) {
            ssize_t nwrote = write(fd_, ptr, remaining);
            if (nwrote != -1) {
                remaining -= nwrote;
                ptr += nwrote;
            } else {
                if (errno == EINTR) continue;
                LogSyserr("Couldn't write() {} bytes to file \"{}\"", sz, path_);
                UnsetFlag(FL_NEEDS_SYNC);
                return -1;
            }
        }
        SetFlag(FL_NEEDS_SYNC);
    }
    return status;
}

bool UnioEntry::IsMatch(const std::vector<std::string>& args) const {
    if (args.empty()) return false;
    return path_ == args.back();
}

}
}
