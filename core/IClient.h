#pragma once

#include "ProdClass.h"
#include "Product.h"

#include <string>
#include <memory>

namespace rdm {
class IServiceHandler;

enum class ClientState {
  NOT_FOUND,
  UNAVAILABLE,
  RESPONDING,
  TIMEOUT,
  SYSTEM_ERROR
};

struct PingStatus {
  ClientState  state;
  double       elapsedSeconds;
  unsigned int port;
  std::string  errorMessage;
};

struct FeedRequest {
  bool           isNotifier;
  unsigned int   maxHereis;
  ProdClass requestedClass;
};

enum class ReplyStatus : int {
  OK            = 0,
  SHUTTING_DOWN = 1,
  BAD_PATTERN   = 2,
  DONT_SEND     = 3,
  RESEND        = 4,
  RESTART       = 5,
  REDIRECT      = 6,
  RECLASS       = 7,
  SYSTEM_ERROR  = 8
};

struct FeedResponse {
  ReplyStatus    statusCode;
  unsigned int   feedProcessId;
  ProdClass allowedClass;
};

struct HiyaRequest {
  ProdClass offeredClass;
};

struct HiyaResponse {
  ReplyStatus    statusCode;
  unsigned int   maxHereis;
  ProdClass acceptedClass;
};

class IClient {
public:
  virtual
  ~IClient() = default;

  virtual int
  Connect() = 0;
  virtual int
  SendProduct(const Product& product) = 0;
  virtual int
  SendNotification(const ProdInfo& info) = 0;
  virtual int
  Flush() = 0;
  virtual void
  Disconnect() = 0;

  virtual int DisableNagles(){ return 0; }

  virtual void SetMaxHereIs(unsigned int max_hereis){ }

  virtual std::string
  GetLastError() const = 0;

  virtual PingStatus
  Ping(unsigned int timeoutSeconds) = 0;

  virtual FeedResponse
  SubscribeAndListen(
    const FeedRequest                & request,
    std::shared_ptr<IServiceHandler> handler,
    unsigned int                     inactiveTimeoutSecs = 30) = 0;
};
}
