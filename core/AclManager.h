#pragma once

#include "ServerConfig.h"
#include "UpFilter.h"
#include "ProdClass.h"

#include <string>
#include <vector>
#include <memory>

namespace rdm {
class AclManager {
private:
  std::vector<AllowRule> allowRules_;
  std::vector<AcceptRule> acceptRules_;

  bool
  IsHostMatch(const std::string& hostName, const std::string& ipAddr, const std::string& pattern) const;

public:

  AclManager(std::vector<AllowRule> allow, std::vector<AcceptRule> accept)
    : allowRules_(std::move(allow)), acceptRules_(std::move(accept)){ }

  ~AclManager() = default;

  bool
  RequiresServer() const;

  bool
  IsHostOk(const std::string& hostName, const std::string& ipAddr) const;

  FeedType
  GetAllowed(const std::string& hostName, const std::string& ipAddr, FeedType desiredFeed) const;

  int
  ReduceToAllowed(const std::string& hostName, const std::string& ipAddr, const ProdClass& want,
    ProdClass& intersect) const;

  int 
  ReduceToAcceptable(const std::string& hostName, const std::string& ipAddr, const ProdClass& offered,
    ProdClass& intersect) const;

  std::shared_ptr<UpFilter>
  GetUpstreamFilter(const std::string& hostName, const std::string& ipAddr, const ProdClass& want) const;
};
}
