#include "PipeBuffer.h"
#include "Log.h"
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <chrono>

namespace rdm {
namespace pqact {

PipeBuffer::PipeBuffer(int fd, size_t bufSize) : fd_(fd) {
    SetNonBlocking();
    buffer_.resize(bufSize);
}

void PipeBuffer::SetNonBlocking() {
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags == -1) {
        LogSyserr("fcntl(..., F_GETFL)");
        return;
    }
    if (!(flags & O_NONBLOCK)) {
        if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == -1) {
            LogSyserr("fcntl(..., F_SETFL, O_NONBLOCK)");
        }
    }
}

int PipeBuffer::Flush(bool block, unsigned int timeoutSecs, const std::string& cmd) {
    if (writePos_ == 0) return 0;

    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeoutSecs * 1000);

    size_t pending = writePos_;
    char* ptr = buffer_.data();

    while (pending > 0) {
        if (block && timeoutSecs != 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
            auto remaining = timeout - elapsed;

            if (remaining.count() <= 0) {
                LogError("write({},,{}) to decoder timed-out ({} s): cmd=\"{}\"", fd_, pending, timeoutSecs, cmd);
                return ETIMEDOUT;
            }

            struct pollfd pfd;
            pfd.fd = fd_;
            pfd.events = POLLOUT;

            int ret = poll(&pfd, 1, remaining.count());
            if (ret == 0) {
                LogError("write({},,{}) to decoder timed-out ({} s): cmd=\"{}\"", fd_, pending, timeoutSecs, cmd);
                return ETIMEDOUT;
            } else if (ret < 0 && errno != EINTR) {
                LogSyserr("poll() failed on pipe to decoder");
                return errno;
            }
        }

        ssize_t nwrote = write(fd_, ptr, pending);
        if (nwrote == -1) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (!block) {
                    std::memmove(buffer_.data(), ptr, pending);
                    writePos_ = pending;
                    return EAGAIN;
                }
                continue;
            }
            LogSyserr("Couldn't write to pipe: fd={}, len={}, cmd=\"{}\"", fd_, pending, cmd);
            return errno;
        }

        pending -= nwrote;
        ptr += nwrote;
    }

    writePos_ = 0;
    return 0;
}

int PipeBuffer::Write(const char* data, size_t nbytes, unsigned int timeoutSecs, const std::string& cmd) {
    int status = 0;
    while (nbytes > 0) {
        size_t avail = buffer_.size() - writePos_;
        size_t chunk = std::min(nbytes, avail);

        std::memcpy(buffer_.data() + writePos_, data, chunk);
        writePos_ += chunk;

        data += chunk;
        nbytes -= chunk;

        if (writePos_ == buffer_.size()) {
            status = Flush(true, timeoutSecs, cmd);
            if (status != 0) return status;
        }
    }

    status = Flush(false, 0, cmd);
    return (status == EAGAIN) ? 0 : status;
}

}
}
