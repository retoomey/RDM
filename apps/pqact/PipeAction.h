#pragma once
#include "IAction.h"

namespace rdm {
namespace pqact {
class PipeAction : public IAction {
public:
  explicit PipeAction(PqactContext& ctx) : IAction(ctx){ }

  int
  Execute(const Product& prod, const std::vector<std::string>& args, const void * xprod, size_t xlen) override;
  const char *
  GetName() const override { return "pipe"; }
};
} // namespace pqact
}
