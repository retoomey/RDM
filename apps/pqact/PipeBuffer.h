#pragma once

#include <vector>
#include <string>
#include <sys/types.h>

namespace rdm {
namespace pqact {
class PipeBuffer {
private:
  int fd_{ -1 };
  std::vector<char> buffer_;
  size_t writePos_{ 0 };

  void
  SetNonBlocking();

public:
  PipeBuffer(int fd, size_t bufSize);
  ~PipeBuffer() = default; // FD is owned and closed by PipeEntry

  int
  Flush(bool block, unsigned int timeoutSecs, const std::string& cmd);
  int
  Write(const char * data, size_t nbytes, unsigned int timeoutSecs, const std::string& cmd);

  int
  GetFd() const { return fd_; }
};
} // namespace pqact
}
