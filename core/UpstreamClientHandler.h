#pragma once

#include "IServiceHandler.h"
#include "IClient.h"
#include "IProductStore.h"
#include "StorageFactory.h"
#include "AclManager.h"
#include "ULDB.h"
#include "ProcessManager.h"
#include "NetworkUtils.h"
#include "UpFilter.h"
#include "PeerContext.h"
#include "Log.h"
#include "Registry.h"
#include "SignalManager.h"
#include "NetworkFactory.h"
#include <memory>
#include <string>
#include <climits>
#include <functional>
#include <arpa/inet.h>
#include <netinet/in.h>

using namespace rdm::network;
using namespace rdm;

namespace rdm {

class UpstreamServerHandler : public IServiceHandler {
private:
  AclManager& aclManager_;
  Uldb& uldb_;
  ProcessManager& processManager_;
  
  std::unique_ptr<IProductStore> managed_pq_;
  bool queue_open_{ false };
  std::shared_ptr<UpFilter> upFilter_;
  ProdClass activeSub_;
  bool isNotifier_{ false };
  bool isPrimary_{ false };

  struct StreamContext {
    UpstreamServerHandler * handler;
    std::shared_ptr<IClient> client;
    int                      sendStatus;
  };

  void ensureQueueOpen() {
    if (!queue_open_) {
      if (managed_pq_->open(registry::getQueuePath().c_str(), PqFlags::Default) == 0) {
        queue_open_ = true;
      } else {
        LogError("Failed to open product queue in UpstreamServerHandler");
      }
    }
  }

  static void stream_callback(const prod_par_t * prod_par, const queue_par_t * queue_par, void * app_par) {
    auto * ctx = static_cast<StreamContext *>(app_par);
    ctx->sendStatus = ctx->handler->ProcessStreamProduct(prod_par, queue_par, ctx->client);
  }

  int ProcessStreamProduct(const prod_par_t * prod_par, const queue_par_t * queue_par, std::shared_ptr<IClient>& client) {
    const ProdInfo * info = &prod_par->info;

    if (upFilter_ && !upFilter_->IsMatch(*info)) { return 0; }

    if (isNotifier_) {
      return client->SendNotification(*info);
    } else {
      Product prod;
      prod.info = *info;
      prod.data = static_cast<const uint8_t *>(prod_par->data);
      return client->SendProduct(prod);
    }
  }

public:
  explicit UpstreamServerHandler(
    AclManager& aclMgr,
    Uldb         & uldb,
    ProcessManager & procMgr)
    : aclManager_(aclMgr), uldb_(uldb), processManager_(procMgr)
  {
      // Allocate the queue dynamically via the plugin factory
      managed_pq_ = StorageFactory::Create(NetworkFactory::CreateSerializer());
  }

  ~UpstreamServerHandler() override {
    if (queue_open_) {
      managed_pq_->close();
    }
  }

  bool IsConnectionAllowed(const std::string& hostname, const std::string& ip) override {
      return aclManager_.IsHostOk(hostname, ip);
  }

  FeedResponse OnFeedRequest(const PeerContext& peer, const FeedRequest& request) override {
    FeedResponse response;
    response.statusCode = ReplyStatus::DONT_SEND;
    isNotifier_         = request.isNotifier;
    isPrimary_ = request.maxHereis > (UINT_MAX / 2);

    upFilter_ = aclManager_.GetUpstreamFilter(
      peer.hostname, peer.ip_string, request.requestedClass);

    if (!upFilter_) {
      return response;
    }

    ProdClass cleanAllowSub;
    int status = aclManager_.ReduceToAllowed(
      peer.hostname, peer.ip_string, request.requestedClass, cleanAllowSub);

    if (status != 0) {
      response.statusCode = ReplyStatus::BAD_PATTERN;
      return response;
    }

    struct sockaddr_storage remote_sockaddr = peer.addr;
    ProdClass uldbSub;
    
    status = static_cast<int>(uldb_.AddProcess(
        getpid(), 6, &remote_sockaddr, cleanAllowSub, uldbSub, isNotifier_, isPrimary_));

    if (status != 0) {
      return response;
    }

    if (!(cleanAllowSub == uldbSub)) {
      response.statusCode = ReplyStatus::RECLASS;
      if (!uldbSub.specs.empty()) {
        uldb_.Remove(getpid());
        response.allowedClass = uldbSub;
      }
    } else {
      response.statusCode    = ReplyStatus::OK;
      response.feedProcessId = getpid();
      activeSub_ = uldbSub;
    }
    return response;
  }

  int StreamProducts(std::shared_ptr<IClient> client) override {
    ensureQueueOpen();
    if (!queue_open_) { return -1; }

    if (client->Connect() != 0) { return -1; }

    StreamContext ctx{ this, client, 0 };
    bool flushNeeded    = false;
    time_t lastSendTime = time(NULL);
    Match mt   = Match::GreaterThan;
    
    auto cursor = managed_pq_->CreateCursor();
    cursor->setCursorClass(&mt, activeSub_);

    while (!SignalManager::IsDone()) {
      ctx.sendStatus = 0;
      int pqStatus = cursor->next(false, activeSub_, stream_callback, false, &ctx);
      if (pqStatus < 0) {
        if (pqStatus == static_cast<int>(PqStatus::End)) {
          if (flushNeeded) {
            if (SignalManager::IsDone()) { break; }
            if (client->Flush() != 0) { return -1; }
            flushNeeded  = false;
            lastSendTime = time(NULL);
          }

          time_t timeSinceLastSend = time(NULL) - lastSendTime;
          auto interval = registry::getSystemInterval();

          if (interval <= timeSinceLastSend) {
            flushNeeded = true;
          } else {
            if (SignalManager::IsDone()) { break; }
            struct timeval tv = { 1, 0 };
            select(0, nullptr, nullptr, nullptr, &tv);
          }
        } else {
          return -1;
        }
      } else if (ctx.sendStatus != 0) {
        return -1;
      } else {
        flushNeeded  = true;
        lastSendTime = time(NULL);
      }
    }

    client->Disconnect();
    return 0;
  }

  bool IsAlive(unsigned int pid) override {
    return processManager_.Contains(static_cast<pid_t>(pid));
  }

  int OnHereIs(const PeerContext& peer, const Product& clean_prod) override { return -1; }
  int OnComingSoon(const PeerContext& peer, const ProdInfo& clean_info, unsigned int pktsz) override { return -1; }
  int OnBlkData(const PeerContext& peer, const uint8_t * signature, unsigned int pktnum, const uint8_t * data, unsigned int size) override { return -1; }
  int OnNotification(const PeerContext& peer, const ProdInfo& info) override { return 0; }
  
  HiyaResponse OnHiyaRequest(const PeerContext& peer, const HiyaRequest& request) override {
    return { ReplyStatus::DONT_SEND, 0, { } };
  }

  pid_t ReapChildProcess() override { return 0; }
};

}
