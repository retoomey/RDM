#include "QueueEntry.h"
#include "MappedRegion.h"
#include <utility>
#include <cerrno>

namespace rdm {

QueueEntry::QueueEntry(std::unique_ptr<MappedRegion> region, pqe_index idx, Callback on_commit, Callback on_rollback)
    : region_(std::move(region)), 
      index_(idx), 
      is_committed_(false), 
      payload_ptr_(nullptr),
      commit_cb_(std::move(on_commit)), 
      rollback_cb_(std::move(on_rollback)) {}

QueueEntry::~QueueEntry() {
    // If it hasn't been committed, we roll back gracefully
    if (!is_committed_ && region_ && rollback_cb_) {
        rollback();
    }
}

QueueEntry::QueueEntry(QueueEntry&& other) noexcept
    : region_(std::move(other.region_)),
      index_(other.index_),
      is_committed_(other.is_committed_),
      payload_ptr_(other.payload_ptr_),
      commit_cb_(std::move(other.commit_cb_)),
      rollback_cb_(std::move(other.rollback_cb_))
{
    other.is_committed_ = false;
    other.payload_ptr_ = nullptr;
    other.commit_cb_ = nullptr;
    other.rollback_cb_ = nullptr;
}

QueueEntry& QueueEntry::operator=(QueueEntry&& other) noexcept {
    if (this != &other) {
        rollback(); // Safely roll back the current lease before taking ownership
        
        region_ = std::move(other.region_);
        index_ = other.index_;
        is_committed_ = other.is_committed_;
        payload_ptr_ = other.payload_ptr_;
        commit_cb_ = std::move(other.commit_cb_);
        rollback_cb_ = std::move(other.rollback_cb_);

        other.is_committed_ = false;
        other.payload_ptr_ = nullptr;
        other.commit_cb_ = nullptr;
        other.rollback_cb_ = nullptr;
    }
    return *this;
}

int QueueEntry::commit() {
    if (is_committed_) return 0;
    if (!commit_cb_ || !region_) return EINVAL;

    // Fire the lambda and pass ownership of the region back
    int status = commit_cb_(index_, std::move(region_));
    if (status == 0) {
        is_committed_ = true;
    }
    return status;
}

int QueueEntry::rollback() {
    if (is_committed_ || !rollback_cb_ || !region_) return 0;

    // Fire the lambda and pass ownership of the region back
    int status = rollback_cb_(index_, std::move(region_));
    if (status == 0) {
        commit_cb_ = nullptr;
        rollback_cb_ = nullptr;
    }
    return status;
}

void* QueueEntry::getWritePointer() const {
    return region_ ? region_->get() : nullptr;
}

size_t QueueEntry::getSize() const {
    return region_ ? region_->getExtent() : 0;
}

bool QueueEntry::isValid() const {
    return region_ != nullptr;
}

}
