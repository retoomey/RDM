#pragma once

#include "FeedType.h"
#include "Pattern.h"
#include "Wordexp.h"

#include <string>
#include <vector>
#include <memory>

namespace rdm {
struct AllowRule {
  FeedType                       feedtype;
  std::string                         hostPattern;
  Pattern                  okPattern;
  std::unique_ptr<Pattern> notPattern;

  AllowRule(FeedType ft, const std::string& hp, const std::string& ok, const std::string& not_pat = "")
    : feedtype(ft), hostPattern(hp), okPattern(ok)
  {
    if (!not_pat.empty()) {
      notPattern = std::make_unique<Pattern>(not_pat);
    }
  }

  AllowRule(const AllowRule& other)
    : feedtype(other.feedtype), hostPattern(other.hostPattern), okPattern(other.okPattern)
  {
    if (other.notPattern) {
      notPattern = std::make_unique<Pattern>(*other.notPattern);
    }
  }

  AllowRule&
  operator = (const AllowRule& other)
  {
    if (this != &other) {
      feedtype    = other.feedtype;
      hostPattern = other.hostPattern;
      okPattern   = other.okPattern;
      if (other.notPattern) {
        notPattern = std::make_unique<Pattern>(*other.notPattern);
      } else {
        notPattern.reset();
      }
    }
    return *this;
  }

  AllowRule(AllowRule&&) noexcept = default;
  AllowRule&
  operator = (AllowRule&&) noexcept = default;
};

struct AcceptRule {
  FeedType      feedtype;
  Pattern prodPattern;
  std::string        hostPattern;
  bool               isPrimary;

  AcceptRule(FeedType ft, const std::string& prod, const std::string& host, bool primary = true)
    : feedtype(ft), prodPattern(prod), hostPattern(host), isPrimary(primary){ }
};

struct RequestRule {
  FeedType feedtype;
  std::string   pattern;
  std::string   upstreamHost;
  unsigned int  port;
};

struct ExecRule {
  Wordexp command;
};

class ServerConfig {
public:
  std::vector<AllowRule> allowRules;
  std::vector<AcceptRule> acceptRules;
  std::vector<RequestRule> requestRules;
  std::vector<ExecRule> execRules;

  bool
  RequiresServer() const
  {
    return !allowRules.empty() || !acceptRules.empty();
  }
};
}
