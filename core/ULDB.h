#pragma once

#include "ProdClass.h"
#include "SemaphoreRWLock.h"
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>

namespace rdm {

enum class UldbStatus {
  SUCCESS = 0,
  ARG,
  INIT,
  EXIST,
  SYSTEM
};

struct UldbEntry {
  pid_t                   pid;
  int                     protoVers;
  int                     isNotifier;
  int                     isPrimary;
  struct sockaddr_storage sockAddr;
  ProdClass               prodClass;
};

// [DEPRECATED] The Uldb singleton instance manager is removed.
// Reason: Singletons create hidden dependencies and global state.
// Uldb should now be instantiated by the Application (e.g., LdmdApp) 
// and injected into the classes that require it.

class Uldb {
private:
  std::string shmName_;
  int shmFd_{-1};
  void* mappedRegion_{nullptr};
  size_t mappedSize_{0};
  SemaphoreRWLock lock_;

  std::string GetPosixShmName(const std::string& path) const;
  UldbStatus AttachSharedMemory();
  void DetachSharedMemory();

public:
  Uldb() = default;
  ~Uldb();

  // Prevents accidental copying of file descriptors and memory maps
  Uldb(const Uldb&) = delete;
  Uldb& operator=(const Uldb&) = delete;

  UldbStatus Create(const std::string& path, unsigned capacity);
  UldbStatus Open(const std::string& path);
  UldbStatus Close();
  UldbStatus Delete(const std::string& path);
  
  UldbStatus GetSize(unsigned& size);
  
  UldbStatus AddProcess(pid_t pid, int protoVers, const struct sockaddr_storage* sockAddr,
                        const ProdClass& desired, ProdClass& allowed,
                        int isNotifier, int isPrimary);
  
  UldbStatus Remove(pid_t pid);
  UldbStatus GetEntries(std::vector<UldbEntry>& outEntries);
  
  UldbStatus ReadLock();
  UldbStatus WriteLock();
  UldbStatus Unlock();
};

} // namespace rdm
