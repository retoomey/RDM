#pragma once
#include <sys/types.h>
#include <cstddef>

namespace rdm {
class RegionManager; // Forward declaration

class MappedRegion {
private:
  RegionManager * manager_;
  off_t offset_;
  size_t extent_;
  int rflags_;
  void * ptr_;

public:
  MappedRegion(RegionManager * mgr, off_t offset, size_t extent, int rflags, void * ptr)
    : manager_(mgr), offset_(offset), extent_(extent), rflags_(rflags), ptr_(ptr){ }

  ~MappedRegion();

  MappedRegion(const MappedRegion&) = delete;
  MappedRegion&
  operator = (const MappedRegion&) = delete;

  MappedRegion(MappedRegion&& other) noexcept;
  MappedRegion&
  operator = (MappedRegion&& other) noexcept;

  void
  markModified();

  void *
  get() const { return ptr_; }

  size_t
  getExtent() const { return extent_; }

  off_t
  getOffset() const { return offset_; }
};
}
