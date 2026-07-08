#pragma once
#include "IServer.h"
#include "IClient.h"
#include "IProductSerializer.h"
#include "ServiceAddr.h"
#include <memory>
#include <string>

namespace rdm {
class NetworkFactory {
public:
  static std::unique_ptr<IServer>
  CreateServer();

  static std::unique_ptr<IClient>
  CreateClient(
    ServiceAddr  target,
    unsigned int timeout_sec);

  static std::unique_ptr<IClient>
  CreateClient(
    ServiceAddr                     target,
    int                             existing_socket,
    const struct sockaddr_storage * remote_addr,
    unsigned int                    timeout_sec);

  static std::shared_ptr<IProductSerializer>
  CreateSerializer();

private:
  static std::string
  GetEngineType();
};
}
