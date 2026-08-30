#pragma once

#include "ServerConfig.h"
#include <string>

namespace rdm {
class ConfParser {
public:

  static bool Parse(const std::string& filepath, ServerConfig& config, unsigned int defaultPort = 388, bool syntaxOnly = false);

private:
  static bool ParseRecursive(const std::string& filepath, ServerConfig& config, int depth, unsigned int defaultPort, bool syntaxOnly = false);

  static std::vector<std::string>
  Tokenize(const std::string& line);
};
}
