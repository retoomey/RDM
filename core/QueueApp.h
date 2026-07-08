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

    queuePath_ = GetOption('q');
    if (queuePath_.empty()) {
      queuePath_ = registry::getDefaultQueuePath();
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
    return true;
  }

public:
  ~QueueApp() override
  {
    if (pq_) { pq_->close(); }
  }
};
}
