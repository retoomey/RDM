#pragma once
#include <string>
#include <unordered_map>
#include <map>
#include <variant>
#include <optional>
#include <stdexcept>

namespace rdm {
namespace registry {

enum class RegistryKey {
  DeleteInfoFiles,
  Hostname,
  InsertionCheckInterval,
  ReconciliationMode,
  CheckTimeEnabled,
  CheckTimeLimit,
  WarnIfCheckTimeDisabled,
  NtpdateCommand,
  NtpdateServers,
  NtpdateTimeout,
  LogCount,
  LogFile,
  LogRotate,
  MetricsCount,
  MetricsFile,
  MetricsFiles,
  NetstatCommand,
  TopCommand,
  PqactConfigPath,
  PqactDatadirPath,
  PqsurfConfigPath,
  PqsurfDatadirPath,
  QueuePath,
  QueueSize,
  QueueSlots,
  ScourConfigPath,
  ScourExcludePath,
  LdmdConfigPath,
  IpAddr,
  MaxClients,
  MaxLatency,
  Port,
  TimeOffset,
  AntiDosEnabled,
  SurfQueuePath,
  SurfQueueSize,
  OessPathname,
  RetxTimeout,
  StatePath,
  TopologyPrefix,
  NetworkEngine,
  StorageEngine,
  RpcTimeout,
  SystemInterval,
  MaxProductSizeBytes
};

using ConfigValue = std::variant<std::string, unsigned, int, bool>;

struct RegistryKeyDefinition {
  std::string xmlPath;
  ConfigValue defaultValue;
};

// ==============================================================================
// Library Consumer Configuration
// ==============================================================================

struct PathOverrides {
    std::optional<std::string> ldmHome;
    std::optional<std::string> registryDir;
    std::optional<std::string> queuePath;
    std::optional<std::string> ldmdConfigPath;
    std::optional<std::string> pqactConfigPath;
    std::optional<std::string> pqactDataDirPath;
    std::optional<std::string> logDir;
    std::optional<std::string> stateDir;
};

void applyOverrides(const PathOverrides& overrides);

// ==============================================================================
// Core Registry API
// ==============================================================================

void setDirectory(const std::string& path);
bool close();
void reset();
void flush();

std::optional<std::string> getString(const std::string& path);
bool putString(const std::string& path, const std::string& value);
bool deleteValue(const std::string& path);
std::map<std::string, std::string> getAllValues(const std::string& path = "/");

std::string getString(RegistryKey key);
unsigned getUint(RegistryKey key);
int getInt(RegistryKey key);
bool getBool(RegistryKey key);

void putString(RegistryKey key, const std::string& value);
void putUint(RegistryKey key, unsigned value);
void putInt(RegistryKey key, int value);
void putBool(RegistryKey key, bool value);
void deleteValue(RegistryKey key);

std::string getDefaultQueuePath();

void setQueuePath(const std::string& path);
std::string getQueuePath();

void setPqactConfigPath(const std::string& path);
std::string getPqactConfigPath();

void setLdmdConfigPath(const std::string& path);
std::string getLdmdConfigPath();

void setPqactDataDirPath(const std::string& path);
std::string getPqactDataDirPath();

void setPqsurfDataDirPath(const std::string& path);
std::string getPqsurfDataDirPath();

void setSurfQueuePath(const std::string& path);
std::string getSurfQueuePath();

void setPqsurfConfigPath(const std::string& path);
std::string getPqsurfConfigPath();

void setLdmHomePath(const std::string& path);
std::string getLdmHomePath();

std::string getSysConfDirPath();
std::string getRegistryDirPath();

void setLdmLogDir(const std::string& path);
std::string getLdmLogDir();

std::string getLdmVarRunDir();
std::string getLdmStateDir();
std::string getTopologyPrefix();

int isAntiDosEnabled();
unsigned int getTimeOffset();
unsigned int getSystemInterval();
unsigned int getMaxProductSizeBytes();

} // namespace registry
} // namespace rdm
