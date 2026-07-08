#pragma once

#include "Product.h"
#include "FeedType.h"
#include "IClient.h"
#include "PeerContext.h"

#include <memory>
#include <cstdint>

#include <sys/types.h>

namespace rdm {
class IClient;

struct FeedRequest;
struct FeedResponse;
struct HiyaRequest;
struct HiyaResponse;

class IServiceHandler {
public:
  virtual
  ~IServiceHandler() = default;

  virtual bool IsConnectionAllowed(const std::string& hostname, const std::string& ip) = 0;
  virtual int
  OnHereIs(const PeerContext& peer, const Product& product) = 0;
  virtual int
  OnComingSoon(const PeerContext& peer, const ProdInfo& info, unsigned int pktsz) = 0;
  virtual int
  OnBlkData(const PeerContext& peer, const uint8_t * signature, unsigned int pktnum, const uint8_t * data,
    unsigned int size) = 0;
  virtual int
  OnNotification(const PeerContext& peer, const ProdInfo& info) = 0;

  virtual HiyaResponse
  OnHiyaRequest(const PeerContext& peer, const HiyaRequest& request) = 0;
  virtual FeedResponse
  OnFeedRequest(const PeerContext& peer, const FeedRequest& request) = 0;

  virtual int
  StreamProducts(std::shared_ptr<IClient> client) = 0;

  virtual bool
  IsAlive(unsigned int pid) = 0;
  virtual pid_t ReapChildProcess(){ return 0; }
};
}
