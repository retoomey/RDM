#pragma once
#include "ServerConfig.h"

#include <string>
#include <map>
#include <mutex>
#include <functional>

#include <sys/types.h>

namespace rdm {
class ProcessManager {
private:
  std::map<pid_t, std::string> activeProcesses_;
  mutable std::mutex mutex_;


public:
  ProcessManager() = default;
  ~ProcessManager() = default;

  ProcessManager(const ProcessManager&) = delete;
  ProcessManager&
  operator = (const ProcessManager&) = delete;

  pid_t
  SpawnExec(const ExecRule& rule);
  pid_t
  SpawnRequester(const std::string& host, std::function<void()> runFunc);

  bool
  Add(pid_t pid, const std::string& description);
  bool
  Remove(pid_t pid);
  bool
  Contains(pid_t pid) const;
  size_t
  Count() const;
  std::string
  GetCommand(pid_t pid) const;

  pid_t
  Reap(pid_t pid, int options, int * outStatus = nullptr);
};
}
