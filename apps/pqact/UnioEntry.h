#pragma once
#include "IFileEntry.h"

namespace rdm {
namespace pqact {
class UnioEntry : public IFileEntry {
private:
  int fd_{ -1 };
  void
  ParseOptions(const std::vector<std::string>& args);

public:
  UnioEntry() = default;
  ~UnioEntry() override { Close(); }

  static constexpr EntryType TYPE = EntryType::Unio;
  EntryType GetType() const override { return TYPE; }

  int
  Open(const std::vector<std::string>& args) override;
  void
  Close() override;
  int
  Sync(bool block) override;
  int
  Write(const Product& prod, const void * data, size_t sz) override;
  bool
  IsMatch(const std::vector<std::string>& args) const override;

  int
  GetFd() const { return fd_; }
};
} // namespace pqact
} 
