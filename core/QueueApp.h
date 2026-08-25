#pragma once
#include "Application.h"
#include "IProductStore.h"
#include "StorageFactory.h"
#include "NetworkFactory.h"
#include "Registry.h"

#include <memory>

namespace rdm {
class QueueApp : public Application {
protected:
  std::string queuePath_;
  std::unique_ptr<IProductStore> pq_;
  int pqOpenFlags_;

  // Track the state version to prevent lost wakeups
  uint64_t lastDataVersion_{0};

  explicit QueueApp(int pqOpenFlags = 0, const std::string& desc = "")
    : Application(desc), pqOpenFlags_(pqOpenFlags){ }

  void
  ConfigureOptions() override
  {
    Application::ConfigureOptions();
    RegisterOption('q', "pqfname", "Path to product queue file", registry::getDefaultQueuePath());
  }

  bool
  ProcessOptions() override
  {
    if (!Application::ProcessOptions()) { return false; }

    // ------------------------------------------------========================
    // 1. QUEUE PATH HIERARCHY RESOLUTION
    // Order: -q flag -> Positional Arg -> Active Registry -> Default Registry
    // ------------------------------------------------========================
    if (IsSet('q') && !GetOption('q').empty()) {
      queuePath_ = GetOption('q');
      registry::setQueuePath(queuePath_); // Explicit override updates registry
    } else if (!positionalArgs_.empty()) {
      queuePath_ = positionalArgs_[0];
      registry::setQueuePath(queuePath_); // Positional override updates registry
    } else {
      // Pull configured path from registry (or system default if unset)
      queuePath_ = registry::getQueuePath();
      if (queuePath_.empty()) {
        queuePath_ = registry::getDefaultQueuePath();
      }
    }

    return true;
  }

  bool
  Initialize() override
  {
    if (!Application::Initialize()) { return false; }
    registry::setQueuePath(queuePath_);
    auto serializer = NetworkFactory::CreateSerializer();
    pq_ = StorageFactory::Create(serializer);
    int status = pq_->open(queuePath_, pqOpenFlags_);
    if (status) {
      if (status == static_cast<int>(PqStatus::Corrupt)) {
        LogError("The product-queue \"{}\" is inconsistent", queuePath_);
      } else {
        LogError("pq_open failed: {}: {}", queuePath_, pq_->strerror(status));
      }
      return false;
    }
    
    // Initialize the baseline version upon opening the queue
    lastDataVersion_ = pq_->getDataVersion();
    return true;
  }

  void WaitOnQueue(unsigned int interval) {
      // Pass the tracked version to the queue
      pq_->waitForData(lastDataVersion_, interval);
      
      // Update our tracked version immediately upon waking up
      lastDataVersion_ = pq_->getDataVersion();
  }

  void NotifyQueue() {
      pq_->notifyReaders();
  }

public:
  ~QueueApp() override
  {
    if (pq_) { pq_->close(); }
  }
};
}
