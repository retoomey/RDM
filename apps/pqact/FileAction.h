#pragma once
#include "IAction.h"

namespace rdm {
namespace pqact {
class FileAction : public IAction {
public:
  explicit FileAction(PqactContext& ctx) : IAction(ctx){ }

  int
  Execute(const Product& prod, const std::vector<std::string>& args, const void * xprod, size_t xlen) override;
  const char *
  GetName() const override { return "file"; }
};
} // namespace pqact
}
