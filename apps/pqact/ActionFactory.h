#pragma once
#include "IAction.h"
#include "PqactContext.h"
#include <memory>
#include <string>

namespace rdm {
namespace pqact {
class ActionFactory {
public:
  static std::unique_ptr<IAction>
  Create(const std::string& actionName, PqactContext& ctx);
};
}
}
