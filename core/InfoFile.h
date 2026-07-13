#pragma once
#include "ProdInfo.h"
#include "ProdClass.h"
#include <string>

namespace rdm {
class SavedInfoFile {
private:
  std::string statePath_;

  // Generates the MD5 checksum from information given
  std::string
  ComputeMD5(
    const std::string& upId, unsigned port, const ProdClass& prodClass);

  // Generates a unique file name (even for network shares)
  void
  ComputePath(const std::string& pathPrefix,
    const std::string& upId, unsigned port, const ProdClass& prodClass);

public:
  SavedInfoFile(const std::string& pathPrefix,
    const std::string& upId, unsigned port, const ProdClass& prodClass);

  // Attempts to read the modern state file
  bool
  Read(ProdInfo& outInfo) const;

  // Losslessly writes the state to the topology-aware path
  bool
  Write(const ProdInfo& info) const;
};
}
