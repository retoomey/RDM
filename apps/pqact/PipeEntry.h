#pragma once

#include "IFileEntry.h"
#include "PipeBuffer.h"
#include <sys/types.h>
#include <memory>
#include <vector>
#include <string>

namespace rdm {
namespace pqact {
class PipeEntry : public IFileEntry {
private:
  std::unique_ptr<PipeBuffer> pbuf_;
  pid_t pid_{ -1 };
  int timeout_{ 60 };

  void
  ParseOptions(const std::vector<std::string>& args, size_t& cmdIdx);

public:
  PipeEntry() = default;
  ~PipeEntry() override { Close(); }

  static constexpr EntryType TYPE = EntryType::Pipe;
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

  void SetTimeout(int timeout){ timeout_ = timeout; }

  pid_t
  GetPid() const { return pid_; }
};
} // namespace pqact
}
