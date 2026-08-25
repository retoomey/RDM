#pragma once
#include "Application.h"
#include "IClient.h"
#include "NetworkFactory.h"
#include "ServiceAddr.h"
#include "Registry.h"
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

    // 1. HOSTNAME RESOLUTION
    if (IsSet('h')) {
      remoteHost_ = GetOption('h');
    } else {
      std::string regHost = registry::getString(registry::RegistryKey::Hostname);
      remoteHost_ = !regHost.empty() ? regHost : "localhost";
    }

    // 2. PORT RESOLUTION
    if (IsSet('P')) {
      port_ = static_cast<unsigned>(std::stoul(GetOption('P')));
    } else {
      unsigned regPort = registry::getUint(registry::RegistryKey::Port);
      port_ = (regPort > 0) ? regPort : 388;
    }

    // 3. TIMEOUT RESOLUTION
    if (IsSet('t')) {
      timeo_ = static_cast<unsigned>(std::stoul(GetOption('t')));
    } else {
      unsigned regTimeo = registry::getUint(registry::RegistryKey::RpcTimeout);
      timeo_ = (regTimeo > 0) ? regTimeo : 60;
    }

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
