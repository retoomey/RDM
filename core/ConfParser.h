#pragma once

#include "ServerConfig.h"
#include <string>

namespace rdm {
class ConfParser {
public:

  static ServerConfig
  Parse(const std::string& filepath, unsigned int defaultPort = 388);

private:
  static bool
  ParseRecursive(const std::string& filepath, ServerConfig& config, int depth, unsigned int defaultPort);
  static std::vector<std::string>
  Tokenize(const std::string& line);
};
}
