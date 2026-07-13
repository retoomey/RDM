#pragma once
#include "Application.h"
#include "IClient.h"
#include "NetworkFactory.h"
#include "ServiceAddr.h"
#include <string>
#include <memory>

namespace rdm {
class NetworkApp : public Application {
protected:
  std::string remoteHost_;
  unsigned port_;
  unsigned timeo_;

  // Setting timeout to 60, double ldmd to avoid early dropout
  explicit NetworkApp(const std::string& desc = "")
    : Application(desc), remoteHost_("localhost"), port_(388), timeo_(60){ }

  void
  ConfigureOptions() override
  {
    Application::ConfigureOptions();

    RegisterOption('h', "remote", "Have 'remote' send us data (default: localhost)", "localhost");
    RegisterOption('P', "port", "Set the port number (default: 388)", "388");
    RegisterOption('t', "timeout", "Set RPC timeout to 'timeout' seconds", "60");
  }

  bool
  ProcessOptions() override
  {
    if (!Application::ProcessOptions()) { return false; }

    if (IsSet('h')) { remoteHost_ = GetOption('h'); }
    if (IsSet('P')) { port_ = std::stoul(GetOption('P')); }
    if (IsSet('t')) { timeo_ = std::stoul(GetOption('t')); }

    return true;
  }

  std::unique_ptr<IClient>
  CreateClient()
  {
    auto sa = ServiceAddr::Parse(remoteHost_, "localhost", port_);

    if (!sa) {
      LogError("Invalid target address: {}", remoteHost_);
      return nullptr;
    }
    return NetworkFactory::CreateClient(std::move(*sa), timeo_);
  }
};
}
