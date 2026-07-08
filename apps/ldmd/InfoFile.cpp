#include "InfoFile.h"
#include "Signature.h"
#include "Log.h"
#include "NetworkUtils.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace rdm {

SavedInfoFile::SavedInfoFile(const std::string& pathPrefix,
  const std::string& upId, unsigned port, const ProdClass& prodClass) {
    ComputePath(pathPrefix, upId, port, prodClass);
}

std::string SavedInfoFile::ComputeMD5(const std::string& upId, unsigned port, const ProdClass& prodClass)
{
    Signature::Hasher hasher;

    hasher.Update(upId.c_str(), upId.length());
    hasher.Update(&port, sizeof(port));

    for (const auto& spec : prodClass.specs) {
        if (spec.feedtype != NONE) {
            hasher.Update(&spec.feedtype, sizeof(FeedType));
            if (!spec.pattern.empty()) {
                hasher.Update(spec.pattern.c_str(), spec.pattern.length());
            }
        }
    }

    return hasher.Finalize().ToString();
}

void SavedInfoFile::ComputePath(const std::string& pathPrefix, const std::string& upId, unsigned port, const ProdClass& prodClass) {
    // Unique hash for prod class
    std::string hashStr = ComputeMD5(upId, port, prodClass);
    // Hostname of the machine
    std::string hostname = network::GetLocalHostName();

    // Final output file name.  Note for folders have / in the prefix
    statePath_ = pathPrefix.empty()? "./": pathPrefix+"/";
    statePath_ += hostname;
    statePath_ += "_";
    statePath_ += hashStr;
    statePath_ += ".info";
    
    LogInfo("Calculated info filename as {}", statePath_);
}

bool SavedInfoFile::Read(ProdInfo& outInfo) const {
    if (statePath_.empty() || !fs::exists(statePath_)) return false;

    std::ifstream file(statePath_);
    if (!file.is_open()) return false;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string sigHex;
        
        if (iss >> outInfo.arrival.tv_sec >> outInfo.arrival.tv_usec
                >> outInfo.feedtype >> outInfo.seqno >> outInfo.sz
                >> sigHex >> outInfo.origin >> std::ws) {
            std::getline(iss, outInfo.ident);
            
            // Modernized Signature parsing
            auto sigOpt = Signature::Parse(sigHex);
            if (sigOpt) {
                outInfo.signature = *sigOpt;
                return true;
            } else {
                LogError("State file corruption: Invalid signature hex in {}", statePath_);
                return false;
            }
        }
    }
    return false;
}

bool SavedInfoFile::Write(const ProdInfo& info) const {
    if (statePath_.empty()) return false;

    std::error_code dir_ec;
    fs::path p(statePath_);
    
    if (!p.parent_path().empty() && !fs::exists(p.parent_path(), dir_ec)) {
        fs::create_directories(p.parent_path(), dir_ec);
        if (dir_ec) {
            LogError("Failed to create state directory {}: {}",
                      p.parent_path().string(), dir_ec.message());
            return false;
        }
    }

    std::string tmpPath = statePath_ + ".tmp";
    std::ofstream file(tmpPath);
    if (!file.is_open()) {
        LogError("Failed to open temporary state file for writing: {}", tmpPath);
        return false;
    }

    file << "# Modern LDM State File\n";
    file << "# <sec> <usec> <feedtype> <seqno> <sz> <sig_hex> <origin> <ident>\n";

    // Modernized file stream writing (no more character array buffers!)
    file << info.arrival.tv_sec << " "
         << info.arrival.tv_usec << " "
         << info.feedtype << " "
         << info.seqno << " "
         << info.sz << " "
         << info.signature.ToString() << " "
         << info.origin << " "
         << info.ident << "\n";
         
    file.close();

    std::error_code ec;
    fs::rename(tmpPath, statePath_, ec);
    if (ec) {
        return false;
    }
    return true;
}

}
