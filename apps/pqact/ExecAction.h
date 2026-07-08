#pragma once
#include "IAction.h"

namespace rdm {
namespace pqact {
class ExecAction : public IAction {
public:
  explicit ExecAction(PqactContext& ctx) : IAction(ctx){ }

  int
  Execute(const Product& prod, const std::vector<std::string>& args, const void * xprod, size_t xlen) override;
  const char *
  GetName() const override { return "exec"; }
};
} // namespace pqact
}
