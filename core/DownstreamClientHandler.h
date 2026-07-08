#pragma once
#include "IServiceHandler.h"
#include "IClient.h"
#include "IProductStore.h"
#include "PeerContext.h"
#include "Log.h"
#include "Registry.h"
#include "SignalManager.h"
#include "Timestamp.h"

#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <functional>

#include <search.h>
#include <signal.h>
#include <unistd.h>

namespace rdm {
class DownstreamClientHandler : public IServiceHandler {
private:
  IProductStore * pq_;
  bool expectBlkdata_{ false };
  Product pendingProd_;
  std::unique_ptr<IQueueEntry> activeQueueEntry_;
  uint8_t * blkMmapPointer_{ nullptr };

  size_t remaining_{ 0 };
  std::function<void(const ProdInfo&, int)> stateCallback_;

  struct FutureEntry {
    time_t start;
    char   hostname[256];
  };

  static int
  compareFutureEntries(const void * entry1, const void * entry2)
  {
    const char * name1 = static_cast<const FutureEntry *>(entry1)->hostname;
    const char * name2 = static_cast<const FutureEntry *>(entry2)->hostname;

    return std::strcmp(name1, name2);
  }

  void
  UpdateSavedInfoAndAutoshift(const ProdInfo& info, int success)
  {
    if (stateCallback_) {
      stateCallback_(info, success);
    }
  }

void vetCreationTimeAndSignal(const ProdInfo& info) {
    (void) kill(0, SIGCONT);
    Timestamp initialSearchTime = info.arrival;
    initialSearchTime.tv_sec -= registry::getSystemInterval();
    Timestamp now = Timestamp::Now();
    
    if (initialSearchTime > now) {
        const std::string& originStr = info.origin;
        size_t pos = originStr.find("_v_");
        std::string truncOrigin = (pos == std::string::npos) ? originStr : originStr.substr(0, pos);
        
        if (log_is_enabled_info) {
            LogWarning("Future product from \"{}\". Fix local or ingest clock. {}",
                       truncOrigin, info.ToString(false));
        } else {
            constexpr int FUTURE_INTERVAL = 3600;
            // RAII compliant, automatically cleans up keys and values on destruction
            // Note: Since this is static, it persists across requests just like the old void* root, 
            // but safely destroys its heap allocations when the process exits.
            static std::unordered_map<std::string, time_t> futureEntries;
            
            time_t nowTime = time(nullptr);
            auto it = futureEntries.find(truncOrigin);
            
            if (it != futureEntries.end()) {
                // Host exists, check if enough time has passed to warn again
                if (it->second <= nowTime) {
                    LogWarning("Future product from \"{}\". Fix local or ingest clock. {}",
                               truncOrigin, info.ToString(false));
                    it->second = nowTime + FUTURE_INTERVAL;
                }
            } else {
                // New host, warn immediately and set the next warning threshold
                LogWarning("Future product from \"{}\". Fix local or ingest clock. {}",
                           truncOrigin, info.ToString(false));
                futureEntries[truncOrigin] = nowTime + FUTURE_INTERVAL;
            }
        }
    }
}

public:
  explicit DownstreamClientHandler(IProductStore * pq,
    std::function<void(const ProdInfo&, int)>               callback = nullptr)
    : pq_(pq), stateCallback_(std::move(callback)){ }

  bool IsConnectionAllowed(const std::string& hostname, const std::string& ip) override { 
      return true; // Downstream clients don't enforce inbound ACLs
  }

  int OnHereIs(const PeerContext& peer, const Product& clean_prod) override {
    if (!pq_) { return static_cast<int>(PqStatus::System); }
    
    // Shallow copy is safe and zero-overhead since 'data' is just a pointer
    Product prod = clean_prod;
    prod.info.origin = network::AppendUpstreamHostToOrigin(clean_prod.info.origin, peer.hostname.c_str());
    
    int error = pq_->insert(prod);
    if (error == 0) {
      vetCreationTimeAndSignal(prod.info);
      UpdateSavedInfoAndAutoshift(prod.info, 1);
    } else if ((error == static_cast<int>(PqStatus::Dup)) || (error == static_cast<int>(PqStatus::Big))) {
      UpdateSavedInfoAndAutoshift(prod.info, 0);
    }
    return error;
  }

  int OnComingSoon(const PeerContext& peer, const ProdInfo& clean_info, unsigned int pktsz) override {
    if (!pq_) { return static_cast<int>(PqStatus::System); }
    if (expectBlkdata_) {
      if (activeQueueEntry_) activeQueueEntry_->rollback(); 
      expectBlkdata_ = false;
    }
    
    // Assign directly instead of field-by-field mapping
    pendingProd_.info = clean_info;
    pendingProd_.info.origin = network::AppendUpstreamHostToOrigin(clean_info.origin, peer.hostname.c_str());
    
    int status = pq_->newElement(pendingProd_.info, activeQueueEntry_);
    if (status == 0) {
      blkMmapPointer_ = static_cast<uint8_t *>(activeQueueEntry_->getPayloadPointer()); 
      expectBlkdata_  = true;
      remaining_      = pendingProd_.info.sz;
    } else if ((status == static_cast<int>(PqStatus::Dup)) ||
               (status == static_cast<int>(PqStatus::Big))) {
      UpdateSavedInfoAndAutoshift(pendingProd_.info, 0);
    }
    return status;
  }

  int
  OnBlkData(const PeerContext& peer, const uint8_t * signature, unsigned int pktnum,
    const uint8_t * data, unsigned int size) override
  {
    if (!pq_ || !expectBlkdata_) { return 0; }
    if (!pendingProd_.info.signature.Equals(signature) || (size > remaining_) ) {
      activeQueueEntry_->rollback();
      expectBlkdata_ = false;
      return EINVAL;
    }

    // STREAMLINING EXECUTED:
    // Write the incoming network frame directly into our memory-mapped file space.
    size_t offset = pendingProd_.info.sz - remaining_;

    std::memcpy(blkMmapPointer_ + offset, data, size);
    remaining_ -= size;

    if (remaining_ == 0) {
      expectBlkdata_ = false;

      // Commit the pre-filled layout to the queue indices!
      int error = activeQueueEntry_->commit();
      if (error == 0) {
        vetCreationTimeAndSignal(pendingProd_.info);
        UpdateSavedInfoAndAutoshift(pendingProd_.info, 1);
      } else {
        activeQueueEntry_->rollback();
        if ((error == static_cast<int>(PqStatus::Dup)) || (error == static_cast<int>(PqStatus::Big))) {
          UpdateSavedInfoAndAutoshift(pendingProd_.info, 0);
        }
      }
      blkMmapPointer_ = nullptr;
      return error;
    }
    return 0;
  } // OnBlkData

  FeedResponse
  OnFeedRequest(const PeerContext& peer, const FeedRequest& request) override
  {
    return { ReplyStatus::DONT_SEND, 0, { } };
  }

  int
  StreamProducts(std::shared_ptr<IClient> client) override { return -1; }

  int
  OnNotification(const PeerContext& peer, const ProdInfo& info) override { return 0; }

  HiyaResponse
  OnHiyaRequest(const PeerContext& peer, const HiyaRequest& request) override
  {
    return { ReplyStatus::DONT_SEND, 0, { } };
  }

  bool
  IsAlive(unsigned int pid) override { return true; }

  pid_t
  ReapChildProcess() override { return 0; }
};
}
