/**
 * @file IFileEntry.h
 * @brief Defines the abstract interface for file and pipe operations within pqact.
 */
#pragma once

#include "Product.h"
#include <string>
#include <vector>
#include <ctime>

namespace rdm {
namespace pqact {

enum class EntryType {
  Unio,
  Pipe
};

// State and configuration flags
constexpr int FL_NEEDS_SYNC  = 1;
constexpr int FL_OVERWRITE   = 2;
constexpr int FL_NOTRANSIENT = 16;
constexpr int FL_STRIP       = 32;
constexpr int FL_LOG         = 64;
constexpr int FL_METADATA    = 128;
constexpr int FL_NODATA      = 256;
constexpr int FL_EDEX        = 512;
constexpr int FL_FLUSH       = 1024;
constexpr int FL_CLOSE       = 2048;
constexpr int FL_STRIPWMO    = 4096;

class IFileEntry {
protected:
  time_t lastUse_;
  int flags_;
  std::string path_;

public:
  IFileEntry() : lastUse_(std::time(nullptr)), flags_(0){ }

  virtual
  ~IFileEntry() = default;

  virtual EntryType GetType() const = 0;
  virtual int
  Open(const std::vector<std::string>& args) = 0;
  virtual void
  Close() = 0;
  virtual int
  Sync(bool block) = 0;
  virtual int
  Write(const Product& prod, const void * data, size_t sz) = 0;
  virtual bool
  IsMatch(const std::vector<std::string>& args) const = 0;

  void Touch(){ lastUse_ = std::time(nullptr); }

  time_t
  GetLastUse() const { return lastUse_; }

  const std::string&
  GetPath() const { return path_; }

  void SetFlag(int flag){ flags_ |= flag; }

  void UnsetFlag(int flag){ flags_ &= ~flag; }

  bool
  IsFlagSet(int flag) const { return (flags_ & flag) != 0; }

  int
  GetFlags() const { return flags_; }
};
} // namespace pqact
}
