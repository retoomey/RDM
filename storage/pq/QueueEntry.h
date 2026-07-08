#pragma once
#include <cstddef>
#include <memory>
#include <functional>
#include "IProductStore.h"

namespace rdm {
class MappedRegion;

class QueueEntry : public IQueueEntry {
public:
  using Callback = std::function<int (const pqe_index&, std::unique_ptr<MappedRegion>)>;
private:
  std::unique_ptr<MappedRegion> region_;
  pqe_index index_;
  bool is_committed_{ false };
  void * payload_ptr_{ nullptr };
  Callback commit_cb_;
  Callback rollback_cb_;
public:
  QueueEntry() = default;
  QueueEntry(std::unique_ptr<MappedRegion> region, pqe_index idx, Callback on_commit, Callback on_rollback);
  ~QueueEntry() override;

  QueueEntry(const QueueEntry&) = delete;
  QueueEntry& operator = (const QueueEntry&) = delete;
  QueueEntry(QueueEntry&& other) noexcept;
  QueueEntry& operator = (QueueEntry&& other) noexcept;

  int commit() override;
  int rollback() override;
  size_t getSize() const override;
  bool isValid() const override;
  void * getWritePointer() const override;
  void * getPayloadPointer() const override { return payload_ptr_; }

  const pqe_index& getIndex() const { return index_; }
  bool committed() const { return is_committed_; }
  void setPayloadPointer(void * ptr){ payload_ptr_ = ptr; }
};
}
