#pragma once
#include "Pattern.h"
#include "ProdInfo.h"
#include "FeedType.h"

#include <vector>
#include <memory>
#include <string>

namespace rdm {
class UpFilter {
private:
  struct Element {
    FeedType                       ft;
    Pattern                  okPattern;
    std::unique_ptr<Pattern> notPattern;

    Element(FeedType f, const Pattern& ok, std::unique_ptr<Pattern> notP)
      : ft(f), okPattern(ok), notPattern(std::move(notP)){ }
  };

  std::vector<std::unique_ptr<Element> > elements_;
  mutable std::string cachedString_;
  mutable bool stringOutOfDate_ = true;

public:
  UpFilter()  = default;
  ~UpFilter() = default;

  void
  AddComponent(FeedType feedtype, const Pattern& okPattern,
    const Pattern * notPattern = nullptr);

  bool
  IsMatch(const ProdInfo& info) const;
  size_t
  GetComponentCount() const;
  const std::string&
  ToString() const;
};
}
