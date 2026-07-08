#pragma once

#include "IProductStore.h"
#include "ProcessManager.h"
#include "FileCache.h"
#include "PqactConfig.h"
#include <memory>

namespace rdm {
namespace pqact {
struct PqactContext {
  int                        pipeTimeo;
  IProductStore * queue;
  std::unique_ptr<FileCache> fileCache;
  // Holds the runtime rules and regexes
  PqactConfig                config;
  ProcessManager&            procMgr; 

  explicit PqactContext(IProductStore * q, size_t maxFdCount, ProcessManager& pm)
    : pipeTimeo(60),
    queue(q),
    fileCache(std::make_unique<FileCache>(maxFdCount)),
    procMgr(pm){ } 

  // Disable copy/move to prevent accidental state duplication
  PqactContext(const PqactContext&) = delete;
  PqactContext&
  operator = (const PqactContext&) = delete;
};
} // namespace pqact
} 
