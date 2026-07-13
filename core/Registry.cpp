/**
 * Registry.cpp
 * * Thread-safe, in-memory caching C++17 implementation of the LDM Registry.
 */
#include "Registry.h"
#include "Log.h"
#include <shared_mutex>
#include <mutex>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <pwd.h>

namespace rdm {
namespace registry {

    // --- 1. Compile-Time Macro Fallbacks ---
    #ifndef LDMHOME_DEFAULT
    #define LDMHOME_DEFAULT "/usr/local/ldm"
    #endif

    #ifndef LDM_QUEUE_PATH_DEFAULT
    #define LDM_QUEUE_PATH_DEFAULT "var/queues/ldm.pq"
    #endif

    #ifndef LDM_CONFIG_PATH_DEFAULT
    #define LDM_CONFIG_PATH_DEFAULT "etc/ldmd.conf"
    #endif

    namespace {
        // Dynamic overrides set via API or CLI flags
        std::string overrideQueuePath;
        std::string overrideLdmHomePath;
        std::string overridePqactConfigPath;
        std::string overridePqsurfConfigPath;
        std::string overrideLdmdConfigPath;
        std::string overridePqactDataDirPath;
        std::string overridePqsurfDataDirPath;
        std::string overrideSurfQueuePath;
        std::string overrideLdmLogDir;
        std::string overrideRegistryDir;
        std::string overrideStateDir;

        // --- 2. Centralized Absolute Path Resolver ---
        static std::string ensureAbsolutePath(const std::string& path) {
            if (path.empty() || path.front() == '/') {
                return path;
            }
            return getLdmHomePath() + "/" + path;
        }

        // --- 3. Unified Resolver ---
        static std::string resolvePath(RegistryKey key, const std::string& apiOverride, const std::string& defaultPath) {
            // 1. API / CLI Control (Highest Priority)
            // OS handles relative overrides locally against CWD
            if (!apiOverride.empty()) {
                return apiOverride; 
            }

            // 2. XML Registry Control
            std::string val = getString(key);
            
            // 3. Fallback Default
            if (val.empty() || val == defaultPath) {
                val = defaultPath;
                LogDebug("Using default pathname for key: \"{}\"", defaultPath);
            }

            // 4. Anchor SYSTEM paths to LDMHOME
            return ensureAbsolutePath(val);
        }
    }

    static const std::unordered_map<RegistryKey, RegistryKeyDefinition> KEY_MAP = {
        { RegistryKey::DeleteInfoFiles,         { "/delete-info-files",             false } },
        { RegistryKey::Hostname,                { "/hostname",                      std::string("") } },
        { RegistryKey::InsertionCheckInterval,  { "/insertion-check-interval",      60u } },
        { RegistryKey::ReconciliationMode,      { "/reconciliation-mode",           std::string("standard") } },
        { RegistryKey::CheckTimeEnabled,        { "/check-time/enabled",            true } },
        { RegistryKey::CheckTimeLimit,          { "/check-time/limit",              1000u } },
        { RegistryKey::WarnIfCheckTimeDisabled, { "/check-time/warn-if-disabled",   true } },
        { RegistryKey::NtpdateCommand,          { "/check-time/ntpdate/command",    std::string("ntpdate") } },
        { RegistryKey::NtpdateServers,          { "/check-time/ntpdate/servers",    std::string("pool.ntp.org") } },
        { RegistryKey::NtpdateTimeout,          { "/check-time/ntpdate/timeout",    10u } },
        { RegistryKey::LogCount,                { "/log/count",                     7u } },
        { RegistryKey::LogFile,                 { "/log/file",                      std::string("var/logs/ldmd.log") } },
        { RegistryKey::LogRotate,               { "/log/rotate",                    true } },
        { RegistryKey::MetricsCount,            { "/metrics/count",                 7u } },
        { RegistryKey::MetricsFile,             { "/metrics/file",                  std::string("var/logs/metrics.txt") } },
        { RegistryKey::MetricsFiles,            { "/metrics/files",                 std::string("") } },
        { RegistryKey::NetstatCommand,          { "/metrics/netstat-command",       std::string("netstat -an") } },
        { RegistryKey::TopCommand,              { "/metrics/top-command",           std::string("top -b -n 1") } },
        { RegistryKey::PqactConfigPath,         { "/pqact/config-path",             std::string("etc/pqact.conf") } },
        { RegistryKey::PqactDatadirPath,        { "/pqact/datadir-path",            std::string("var/data") } },
        { RegistryKey::PqsurfConfigPath,        { "/pqsurf/config-path",            std::string("etc/pqsurf.conf") } },
        { RegistryKey::PqsurfDatadirPath,       { "/pqsurf/datadir-path",           std::string("var/data") } },
        { RegistryKey::QueuePath,               { "/queue/path",                    std::string(LDM_QUEUE_PATH_DEFAULT) } },
        { RegistryKey::QueueSize,               { "/queue/size",                    500000000u } },
        { RegistryKey::QueueSlots,              { "/queue/slots",                   100000u } },
        { RegistryKey::ScourConfigPath,         { "/scour/config-path",             std::string("etc/scour.conf") } },
        { RegistryKey::ScourExcludePath,        { "/scour/exclude-path",            std::string("etc/scour_excludes.conf") } },
        { RegistryKey::LdmdConfigPath,          { "/server/config-path",            std::string(LDM_CONFIG_PATH_DEFAULT) } },
        { RegistryKey::IpAddr,                  { "/server/ip-addr",                std::string("0.0.0.0") } },
        { RegistryKey::MaxClients,              { "/server/max-clients",            256u } },
        { RegistryKey::MaxLatency,              { "/server/max-latency",            3600u } },
        { RegistryKey::Port,                    { "/server/port",                   388u } },
        { RegistryKey::TimeOffset,              { "/server/time-offset",            -1 } },
        { RegistryKey::AntiDosEnabled,          { "/server/enable-anti-DOS",        true } },
        { RegistryKey::SurfQueuePath,           { "/surf-queue/path",               std::string("var/queues/pqsurf.pq") } },
        { RegistryKey::SurfQueueSize,           { "/surf-queue/size",               2000000u } },
        { RegistryKey::OessPathname,            { "/oess-pathname",                 std::string("") } },
        { RegistryKey::RetxTimeout,             { "/fmtp-retx-timeout",             30u } },
        { RegistryKey::StatePath,               { "/state-path",                    std::string("var/run") } },
        { RegistryKey::TopologyPrefix,          { "/topology-prefix",               std::string("ldm") } },
        { RegistryKey::NetworkEngine,           { "/server/network-engine",         std::string("sunrpc") } },
        { RegistryKey::StorageEngine,           { "/server/storage-engine",         std::string("pq") } },
        { RegistryKey::RpcTimeout,              { "/server/rpc-timeout",            60u } },
        { RegistryKey::SystemInterval,          { "/system/interval",               30u } },
        { RegistryKey::MaxProductSizeBytes,     { "/server/max-product-size-bytes", 500000000u } }
    };

    static const RegistryKeyDefinition& getDef(RegistryKey key) {
        auto it = KEY_MAP.find(key);
        if (it == KEY_MAP.end()) {
            throw std::invalid_argument("Unregistered configuration key requested.");
        }
        return it->second;
    }

    class RegistryEngine {
    private:
        std::string registryDir_;
        xmlDocPtr doc_;
        mutable std::shared_mutex rw_mutex_;
        bool isDirty_;

        RegistryEngine() : doc_(nullptr), isDirty_(false) {
            xmlInitParser();
            LIBXML_TEST_VERSION
        }
        ~RegistryEngine() {
            close();
            xmlCleanupParser();
        }

        std::string getRegistryFilePath() const {
            // 1. If explicitly set (e.g., via the test suite or applyOverrides), respect it.
            if (!registryDir_.empty()) {
                return registryDir_ + "/registry.xml";
            }
            
            // 2. Otherwise, lazy-evaluate the path using the new Environment/API bounds
            return getRegistryDirPath() + "/registry.xml";
        }

        bool ensureLoaded(bool createIfMissing) {
            {
                std::shared_lock<std::shared_mutex> readLock(rw_mutex_);
                if (doc_ || (!createIfMissing && access(getRegistryFilePath().c_str(), F_OK) == -1)) {
                    return true;
                }
            }
            std::unique_lock<std::shared_mutex> writeLock(rw_mutex_);
            if (doc_) return true;

            std::string filepath = getRegistryFilePath();
            if (!createIfMissing && access(filepath.c_str(), F_OK) == -1) return false;
            
            if (access(filepath.c_str(), F_OK) != -1) {
                doc_ = xmlParseFile(filepath.c_str());
            }

            if (!doc_) {
                if (!createIfMissing) return false;
                doc_ = xmlNewDoc(BAD_CAST "1.0");
                xmlNodePtr root = xmlNewDocNode(doc_, nullptr, BAD_CAST "registry", nullptr);
                xmlDocSetRootElement(doc_, root);
            }
            isDirty_ = false;
            return true;
        }

        void flushToDiskUnlocked() {
            if (isDirty_ && doc_) {
                xmlSaveFormatFile(getRegistryFilePath().c_str(), doc_, 1);
                isDirty_ = false;
            }
        }

        xmlNodePtr findNodeUnlocked(const std::string& path, bool createIfMissing) {
            xmlNodePtr current = xmlDocGetRootElement(doc_);
            if (!current || path.empty() || path == "/") return current;

            std::string remainingPath = (path[0] == '/') ? path.substr(1) : path;
            size_t pos = 0;
            while ((pos = remainingPath.find('/')) != std::string::npos || !remainingPath.empty()) {
                std::string token = remainingPath.substr(0, pos);
                if (pos != std::string::npos) {
                    remainingPath = remainingPath.substr(pos + 1);
                } else {
                    remainingPath.clear();
                }

                if (token.empty()) continue;

                xmlNodePtr child = current->children;
                bool found = false;
                while (child) {
                    if (child->type == XML_ELEMENT_NODE && xmlStrcmp(child->name, BAD_CAST token.c_str()) == 0) {
                        current = child;
                        found = true;
                        break;
                    }
                    child = child->next;
                }

                if (!found) {
                    if (createIfMissing) {
                        current = xmlNewTextChild(current, nullptr, BAD_CAST token.c_str(), nullptr);
                        isDirty_ = true;
                    } else {
                        return nullptr;
                    }
                }
            }
            return current;
        }

        void gatherValuesUnlocked(xmlNodePtr node, const std::string& currentPath, std::map<std::string, std::string>& result) const {
            bool hasChildren = false;
            for (xmlNodePtr child = node->children; child; child = child->next) {
                if (child->type == XML_ELEMENT_NODE) {
                    hasChildren = true;
                    std::string childName = reinterpret_cast<const char*>(child->name);
                    std::string nextPath = (currentPath == "/" ? "" : currentPath) + "/" + childName;
                    gatherValuesUnlocked(child, nextPath, result);
                }
            }

            if (!hasChildren) {
                xmlChar* content = xmlNodeGetContent(node);
                if (content) {
                    result[currentPath.empty() ? "/" : currentPath] = reinterpret_cast<const char*>(content);
                    xmlFree(content);
                }
            }
        }

    public:
        static RegistryEngine& getInstance() {
            static RegistryEngine instance;
            return instance;
        }

        void setDirectory(const std::string& dir) {
            std::unique_lock<std::shared_mutex> lock(rw_mutex_);
            if (doc_) {
                flushToDiskUnlocked();
                xmlFreeDoc(doc_);
                doc_ = nullptr;
            }
            registryDir_ = dir;
        }

        void flushToDisk() {
            std::unique_lock<std::shared_mutex> lock(rw_mutex_);
            flushToDiskUnlocked();
        }

        bool close() {
            std::unique_lock<std::shared_mutex> lock(rw_mutex_);
            if (doc_) {
                flushToDiskUnlocked();
                xmlFreeDoc(doc_);
                doc_ = nullptr;
            }
            return true;
        }

        void reset() {
            std::unique_lock<std::shared_mutex> lock(rw_mutex_);
            if (doc_) {
                xmlFreeDoc(doc_);
                doc_ = nullptr;
            }
            unlink(getRegistryFilePath().c_str());
            isDirty_ = false;
        }

        std::optional<std::string> getString(const std::string& path) {
            if (path.find(' ') != std::string::npos) return std::nullopt;
            ensureLoaded(false);

            std::shared_lock<std::shared_mutex> lock(rw_mutex_);
            if (!doc_) return std::nullopt;

            xmlNodePtr node = findNodeUnlocked(path, false);
            if (!node) return std::nullopt;

            for (xmlNodePtr child = node->children; child; child = child->next) {
                if (child->type == XML_ELEMENT_NODE) return std::nullopt;
            }

            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                std::string result(reinterpret_cast<const char*>(content));
                xmlFree(content);
                return result;
            }
            return std::nullopt;
        }

        bool putString(const std::string& path, const std::string& value) {
            if (path.find(' ') != std::string::npos) {
                LogError("Registry key \"{}\" cannot contain a space", path);
                return false;
            }
            ensureLoaded(true);

            std::unique_lock<std::shared_mutex> lock(rw_mutex_);
            xmlNodePtr node = findNodeUnlocked(path, true);
            if (!node) return false;

            xmlNodeSetContent(node, BAD_CAST value.c_str());
            isDirty_ = true;
            return true;
        }

        bool deleteValue(const std::string& path) {
            ensureLoaded(false);
            std::unique_lock<std::shared_mutex> lock(rw_mutex_);

            xmlNodePtr node = findNodeUnlocked(path, false);
            if (!node) return false;

            xmlUnlinkNode(node);
            xmlFreeNode(node);
            isDirty_ = true;
            return true;
        }

        std::map<std::string, std::string> getAllValues(const std::string& path) {
            std::map<std::string, std::string> result;
            ensureLoaded(false);
            std::shared_lock<std::shared_mutex> lock(rw_mutex_);
            
            if (!doc_) return result;
            
            xmlNodePtr node = findNodeUnlocked(path, false);
            if (!node) return result;

            std::string basePath = (path == "/") ? "" : path;
            gatherValuesUnlocked(node, basePath, result);
            return result;
        }
    };

    // ==============================================================================
    // Application Initialization API
    // ==============================================================================

    void applyOverrides(const PathOverrides& overrides) {
        if (overrides.ldmHome) setLdmHomePath(*overrides.ldmHome);
        if (overrides.registryDir) {
            overrideRegistryDir = *overrides.registryDir;
            // Update the Engine's context immediately if the directory override changes
            setDirectory(getRegistryDirPath());
        }
        if (overrides.queuePath) setQueuePath(*overrides.queuePath);
        if (overrides.ldmdConfigPath) setLdmdConfigPath(*overrides.ldmdConfigPath);
        if (overrides.pqactConfigPath) setPqactConfigPath(*overrides.pqactConfigPath);
        if (overrides.pqactDataDirPath) setPqactDataDirPath(*overrides.pqactDataDirPath);
        if (overrides.logDir) setLdmLogDir(*overrides.logDir);
        if (overrides.stateDir) overrideStateDir = *overrides.stateDir;
    }

    // ==============================================================================
    // System Operations
    // ==============================================================================
    
    void setDirectory(const std::string& path) { RegistryEngine::getInstance().setDirectory(path); }
    bool close() { return RegistryEngine::getInstance().close(); }
    void reset() { RegistryEngine::getInstance().reset(); }
    void flush() { RegistryEngine::getInstance().flushToDisk(); }

    // ==============================================================================
    // Dynamic Path API
    // ==============================================================================
    
    std::optional<std::string> getString(const std::string& path) {
        return RegistryEngine::getInstance().getString(path);
    }
    bool putString(const std::string& path, const std::string& value) {
        return RegistryEngine::getInstance().putString(path, value);
    }
    bool deleteValue(const std::string& path) {
        return RegistryEngine::getInstance().deleteValue(path);
    }
    std::map<std::string, std::string> getAllValues(const std::string& path) {
        return RegistryEngine::getInstance().getAllValues(path);
    }

    // ==============================================================================
    // Strongly-Typed Enum API
    // ==============================================================================
    
    std::string getString(RegistryKey key) {
        const auto& def = getDef(key);
        auto optVal = RegistryEngine::getInstance().getString(def.xmlPath);
        if (optVal) return *optVal;

        LogDebug("RegistryKey {} not found in registry, using default.", def.xmlPath);
        return std::get<std::string>(def.defaultValue);
    }

    unsigned getUint(RegistryKey key) {
        const auto& def = getDef(key);
        auto optVal = RegistryEngine::getInstance().getString(def.xmlPath);
        if (optVal) {
            try { return static_cast<unsigned>(std::stoul(*optVal, nullptr, 0)); }
            catch (...) { LogWarning("Invalid uint in registry for {}", def.xmlPath); }
        }
        return std::get<unsigned>(def.defaultValue);
    }

    int getInt(RegistryKey key) {
        const auto& def = getDef(key);
        auto optVal = RegistryEngine::getInstance().getString(def.xmlPath);
        if (optVal) {
            try { return std::stoi(*optVal, nullptr, 0); }
            catch (...) { LogWarning("Invalid int in registry for {}", def.xmlPath); }
        }
        return std::get<int>(def.defaultValue);
    }

    bool getBool(RegistryKey key) {
        const auto& def = getDef(key);
        auto optVal = RegistryEngine::getInstance().getString(def.xmlPath);
        if (optVal) {
            if (strcasecmp(optVal->c_str(), "TRUE") == 0 || *optVal == "1") return true;
            if (strcasecmp(optVal->c_str(), "FALSE") == 0 || *optVal == "0") return false;
        }
        return std::get<bool>(def.defaultValue);
    }

    void putString(RegistryKey key, const std::string& value) {
        RegistryEngine::getInstance().putString(getDef(key).xmlPath, value);
    }
    void putUint(RegistryKey key, unsigned value) { putString(key, std::to_string(value)); }
    void putInt(RegistryKey key, int value) { putString(key, std::to_string(value)); }
    void putBool(RegistryKey key, bool value) { putString(key, value ? "TRUE" : "FALSE"); }
    void deleteValue(RegistryKey key) { RegistryEngine::getInstance().deleteValue(getDef(key).xmlPath); }

    // ==============================================================================
    // Path Generation & Anchoring
    // ==============================================================================

    std::string getLdmHomePath() {
        if (!overrideLdmHomePath.empty()) {
            return overrideLdmHomePath;
        }
        const char* envHome = std::getenv("LDMHOME");
        if (envHome && std::strlen(envHome) > 0) {
            return std::string(envHome);
        }
        const char* homeDir = std::getenv("HOME");
        if (!homeDir) {
            struct passwd* pw = getpwuid(getuid());
            if (pw && pw->pw_dir) {
                homeDir = pw->pw_dir;
            }
        }
        if (homeDir) {
            return std::string(homeDir) + "/ldm";
        }
        return LDMHOME_DEFAULT;
    }

    std::string getSysConfDirPath() {
        return getLdmHomePath() + "/etc";
    }

    std::string getRegistryDirPath() {
        if (!overrideRegistryDir.empty()) {
            return ensureAbsolutePath(overrideRegistryDir);
        }
        const char* envRegDir = std::getenv("LDM_REGISTRY_DIR");
        if (envRegDir && std::strlen(envRegDir) > 0) {
            return ensureAbsolutePath(std::string(envRegDir));
        }
        return getSysConfDirPath();
    }

    std::string getDefaultQueuePath() {
        return getString(RegistryKey::QueuePath);
    }

    void setQueuePath(const std::string& path) { overrideQueuePath = path; }
    std::string getQueuePath() {
        return resolvePath(RegistryKey::QueuePath, overrideQueuePath, LDM_QUEUE_PATH_DEFAULT);
    }

    void setPqactConfigPath(const std::string& path) { overridePqactConfigPath = path; }
    std::string getPqactConfigPath() {
        return resolvePath(RegistryKey::PqactConfigPath, overridePqactConfigPath, "etc/pqact.conf");
    }

    void setLdmdConfigPath(const std::string& path) { overrideLdmdConfigPath = path; }
    std::string getLdmdConfigPath() {
        return resolvePath(RegistryKey::LdmdConfigPath, overrideLdmdConfigPath, LDM_CONFIG_PATH_DEFAULT);
    }

    void setPqactDataDirPath(const std::string& path) { overridePqactDataDirPath = path; }
    std::string getPqactDataDirPath() {
        return resolvePath(RegistryKey::PqactDatadirPath, overridePqactDataDirPath, "var/data");
    }

    void setPqsurfDataDirPath(const std::string& path) { overridePqsurfDataDirPath = path; }
    std::string getPqsurfDataDirPath() {
        return resolvePath(RegistryKey::PqsurfDatadirPath, overridePqsurfDataDirPath, "var/data");
    }

    void setSurfQueuePath(const std::string& path) { overrideSurfQueuePath = path; }
    std::string getSurfQueuePath() {
        return resolvePath(RegistryKey::SurfQueuePath, overrideSurfQueuePath, "var/queues/pqsurf.pq");
    }

    void setPqsurfConfigPath(const std::string& path) { overridePqsurfConfigPath = path; }
    std::string getPqsurfConfigPath() {
        return resolvePath(RegistryKey::PqsurfConfigPath, overridePqsurfConfigPath, "etc/pqsurf.conf");
    }

    void setLdmHomePath(const std::string& path) { overrideLdmHomePath = path; }

    void setLdmLogDir(const std::string& path) { overrideLdmLogDir = path; }
    std::string getLdmLogDir() {
        std::string dir = overrideLdmLogDir.empty() ? "var/logs" : overrideLdmLogDir;
        return ensureAbsolutePath(dir);
    }

    std::string getLdmVarRunDir() {
       return getLdmHomePath() + "/var/run";
    }

    std::string getLdmStateDir() {
        std::string dir = overrideStateDir.empty() ? getString(RegistryKey::StatePath) : overrideStateDir;
        return ensureAbsolutePath(dir);
    }

    std::string getTopologyPrefix() {
        static std::string prefix = getString(RegistryKey::TopologyPrefix);
        return prefix;
    }

    int isAntiDosEnabled() {
        static int isEnabled = getBool(RegistryKey::AntiDosEnabled) ? 1 : 0;
        return isEnabled;
    }

    unsigned int getTimeOffset() {
        int rawOffset = getInt(RegistryKey::TimeOffset);
        if (rawOffset < 0) {
            unsigned int maxLatency = getUint(RegistryKey::MaxLatency);
            return maxLatency;
        }
        return static_cast<unsigned int>(rawOffset);
    }

    unsigned int getSystemInterval() {
      static unsigned int cachedInterval = getUint(RegistryKey::SystemInterval);
      return cachedInterval;
    }

    unsigned int getMaxProductSizeBytes() {
      static unsigned int cachedMaxSize = getUint(RegistryKey::MaxProductSizeBytes);
      return cachedMaxSize;
    }

}
}
