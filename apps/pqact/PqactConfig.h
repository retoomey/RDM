#pragma once
#include "FeedType.h"
#include "IAction.h"
#include "Pattern.h"
#include <string>
#include <vector>
#include <memory>

namespace rdm {
namespace pqact {
struct PqactContext;

struct PqactEntry {
  FeedType            feedtype;
  Pattern       prog;
  std::unique_ptr<IAction> action;
  std::string              args;

  PqactEntry(FeedType ft, const std::string& pat, std::unique_ptr<IAction> act, std::string a);
  ~PqactEntry() = default;

  PqactEntry(const PqactEntry&) = delete;
  PqactEntry&
  operator = (const PqactEntry&) = delete;

  int
  Execute(const Product& prod, const void * xprod, size_t xlen);
};

class PqactConfig {
public:
  std::vector<std::unique_ptr<PqactEntry> > entries;

  void
  ProcessProduct(const Product& prod, PqactContext& ctx, const void * xprod, size_t xlen, bool& didMatch,
    bool& errorOccurred);
  void Clear(){ entries.clear(); }
};
}
}
