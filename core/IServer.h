#pragma once
#include "IServiceHandler.h"
#include "ProcessManager.h"

#include <string>
#include <memory>

namespace rdm {
class IServer {
public:
  virtual
  ~IServer() = default;

  virtual int Start(const std::string& ip_addr, unsigned int port, unsigned int max_clients,
    std::shared_ptr<IServiceHandler> handler, ProcessManager& procMgr) = 0;

  virtual void
  Stop() = 0;
};
}
