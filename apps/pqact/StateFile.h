#pragma once

#include "FeedType.h"
#include "Timestamp.h"

#include <string>

namespace rdm {
namespace os {
class StateFile {
public:

  /**
   * @brief Reads the last processed timestamp from the state file.
   * @param configPath The path to the configuration file (e.g., pqact.conf).
   * The state file will be derived as configPath + ".state".
   * @param outTimestamp The timestamp object to populate.
   * @return true on success, false if the file doesn't exist or is invalid.
   */
  static bool
  Read(const std::string& configPath, Timestamp& outTimestamp);

  /**
   * @brief Safely writes the timestamp to the state file using atomic renaming.
   * @param configPath The path to the configuration file.
   * @param timestamp The timestamp to save.
   * @return true on success, false on error.
   */
  static bool
  Write(const std::string& configPath, const Timestamp& timestamp);
};
} // namespace os
} 
