#pragma once

#include "PqactConfig.h"
#include "PqactContext.h"
#include <string>

namespace rdm {
namespace pqact {
class PqactParser {
public:
  static bool
  Parse(const std::string& filepath, PqactContext& ctx, PqactConfig& config);
};
} // namespace pqact
}
