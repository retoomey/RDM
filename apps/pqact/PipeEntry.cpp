#include "PipeEntry.h"
#include "Log.h"
#include "ProcessUtil.h"
#include "PrivilegeManager.h"
#include "FileOpsUtil.h"

#include <csignal>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>

namespace rdm {
namespace pqact {

void PipeEntry::ParseOptions(const std::vector<std::string>& args, size_t& cmdIdx) {
    flags_ = FL_NOTRANSIENT; // Pipes are not transient by default
    cmdIdx = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-transient") { UnsetFlag(FL_NOTRANSIENT); cmdIdx++; }
        else if (args[i] == "-strip") { SetFlag(FL_STRIP); cmdIdx++; }
        else if (args[i] == "-metadata") { SetFlag(FL_METADATA); cmdIdx++; }
        else if (args[i] == "-nodata") { SetFlag(FL_NODATA); SetFlag(FL_METADATA); cmdIdx++; }
        else if (args[i] == "-removewmo") { SetFlag(FL_STRIPWMO); cmdIdx++; }
        else if (args[i] == "-flush") { SetFlag(FL_FLUSH); cmdIdx++; }
        else if (args[i] == "-close") { SetFlag(FL_CLOSE); cmdIdx++; }
        else break; // End of options, command begins
    }
}

int PipeEntry::Open(const std::vector<std::string>& args) {
    if (args.empty()) return -1;

    size_t cmdIdx = 0;
    ParseOptions(args, cmdIdx);

    if (cmdIdx >= args.size()) {
        LogError("No command provided for pipe");
        return -1;
    }

    path_.clear();
    std::vector<char*> c_args;
    for (size_t i = cmdIdx; i < args.size(); ++i) {
        if (!path_.empty()) path_ += " ";
        path_ += args[i];
        c_args.push_back(const_cast<char*>(args[i].c_str()));
    }
    c_args.push_back(nullptr);

    int pfd[2];
    if (pipe(pfd) == -1) {
        LogSyserr("Couldn't create pipe");
        return -1;
    }

    if (os::ensureCloseOnExec(pfd[1]) != 0) {
        LogError("Couldn't set write-end of pipe to close on exec()");
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }

    os::ExecParams params;
    params.argv = c_args.data();
    params.setPgid = true;
    params.stdinFd = pfd[0]; // Map the read-end of the pipe to STDIN
    params.resetSignals = true;

    pid_ = os::ForkAndExec(params);
    if (pid_ == -1) {
        LogSyserr("Couldn't fork(2) PIPE process");
        close(pfd[0]);
        close(pfd[1]);
        return -1;
    }

    // Close the read-end in the parent, as it belongs to the child now
    close(pfd[0]);

    long pipe_buf = 512;
#ifdef _PC_PIPE_BUF
    pipe_buf = fpathconf(pfd[1], _PC_PIPE_BUF);
    if (pipe_buf == -1L) pipe_buf = 512;
#endif

    pbuf_ = std::make_unique<PipeBuffer>(pfd[1], pipe_buf);
    LogDebug("Opened pipe fd {} for {} (pid {})", pfd[1], path_, pid_);

    return 0;
}

void PipeEntry::Close() {
    if (pbuf_) {
        if (pid_ >= 0 && IsFlagSet(FL_NEEDS_SYNC)) {
            Sync(true);
        }
        int pfd = pbuf_->GetFd();
        pbuf_.reset(); // Unique_ptr deletes the PipeBuffer object
        if (pfd != -1) close(pfd);
    }
    pid_ = -1;
}

int PipeEntry::Sync(bool block) {
    if (!pbuf_ || !IsFlagSet(FL_NEEDS_SYNC)) return 0;
    
    int status = pbuf_->Flush(block, timeout_, path_);
    if (status == 0) {
        UnsetFlag(FL_NEEDS_SYNC);
        return 0;
    }
    
    if (status == EAGAIN) return 0;
    
    if (status != EINTR) {
        LogError("Couldn't flush I/O to decoder: pid={}, cmd=\"{}\"", pid_, path_);
        UnsetFlag(FL_NEEDS_SYNC);
    }
    return status;
}

int PipeEntry::Write(const Product& prod, const void* data, size_t sz) {
    int status = 0;
    
    if (IsFlagSet(FL_METADATA)) {
        std::vector<uint8_t> metaBuf = FileOpsUtil::SerializeMetadata(prod.info, sz);
        status = pbuf_->Write(reinterpret_cast<const char*>(metaBuf.data()), metaBuf.size(), timeout_, path_);
        if (status) {
            LogError("Couldn't write product metadata to pipe");
        }
    }
    
    if (status == 0 && !IsFlagSet(FL_NODATA) && sz > 0) {
        status = pbuf_->Write(static_cast<const char*>(data), sz, timeout_, path_);
        if (status && status != EINTR) {
            UnsetFlag(FL_NEEDS_SYNC);
        } else {
            SetFlag(FL_NEEDS_SYNC);
        }
    }
    return status;
}

bool PipeEntry::IsMatch(const std::vector<std::string>& args) const {
    size_t cmdIdx = 0;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-transient" || args[i] == "-strip" || args[i] == "-metadata" ||
            args[i] == "-nodata" || args[i] == "-removewmo" || args[i] == "-flush" || args[i] == "-close") {
            cmdIdx++;
        } else {
            break;
        }
    }
    std::string testPath;
    for (size_t i = cmdIdx; i < args.size(); ++i) {
        if (!testPath.empty()) testPath += " ";
        testPath += args[i];
    }
    return path_ == testPath;
}

} // namespace pqact
} 
