#include "StateFile.h"
#include "Log.h"
#include "Registry.h"

#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace rdm {
namespace os {

std::string getStatePath(const std::string& configPath) {
    // Extract just the filename (e.g., "pqact.conf" from "/etc/ldm/pqact.conf")
    char* pathCopy = strdup(configPath.c_str());
    std::string baseName = basename(pathCopy);
    free(pathCopy);

    // Modern Container-Safe Path: /ldmcache/.k8s-pod-1.pqact.conf.state
    return std::string(registry::getLdmStateDir()) + "/." + 
           registry::getTopologyPrefix() + "." + baseName + ".state";
}

bool StateFile::Read(const std::string& configPath, Timestamp& outTimestamp) {
    if (configPath.empty()) {
        LogError("Configuration path is empty; cannot derive state file path.");
        return false;
    }

    std::string statePath = configPath + ".state";
    
    if (!fs::exists(statePath)) {
        return false; // Not an error, just doesn't exist yet (e.g., first run)
    }

    std::ifstream file(statePath);
    if (!file.is_open()) {
        LogError("Couldn't open state file \"{}\" for reading.", statePath);
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comment lines or empty lines
        if (line.empty() || line[0] == '#') continue;

        size_t dotPos = line.find('.');
        if (dotPos == std::string::npos) {
            LogError("Invalid timestamp format in \"{}\"", statePath);
            return false;
        }

        try {
            outTimestamp.tv_sec = std::stoll(line.substr(0, dotPos));
            outTimestamp.tv_usec = std::stoi(line.substr(dotPos + 1));
            
            if (outTimestamp.tv_sec < 0) {
                LogError("Negative seconds value in \"{}\"", statePath);
                return false;
            }
            return true;
        } catch (const std::exception& e) {
            LogError("Failed to parse time from \"{}\": {}", statePath, e.what());
            return false;
        }
    }

    return false;
}

bool StateFile::Write(const std::string& configPath, const Timestamp& timestamp) {
    if (configPath.empty()) {
        LogError("Configuration path is empty; cannot derive state file path.");
        return false;
    }

    std::string statePath = getStatePath(configPath);
    std::string tmpStatePath = statePath + ".tmp";

    std::ofstream file(tmpStatePath);
    if (!file.is_open()) {
        LogError("Couldn't open temporary state file \"{}\" for writing.", tmpStatePath);
        return false;
    }

    file << "# The following line contains the insertion-time of the last, successfully-\n"
         << "# processed data-product.  Do not modify it unless you know exactly what\n"
         << "# you're doing!\n";

    // Write formatted timestamp: SEC.USEC (pad microseconds with zeros)
    char timeStr[64];
    std::snprintf(timeStr, sizeof(timeStr), "%ld.%06d\n", 
                  static_cast<long>(timestamp.tv_sec), 
                  static_cast<int>(timestamp.tv_usec));
    file << timeStr;
    file.close();

    // Atomic rename replaces the old state file safely
    std::error_code ec;
    fs::rename(tmpStatePath, statePath, ec);
    if (ec) {
        LogError("Couldn't rename \"{}\" to \"{}\": {}", 
                  tmpStatePath, statePath, ec.message());
        return false;
    }
    LogInfo("Wrote state file: {}", statePath);

    return true;
}

} // namespace os
} 
