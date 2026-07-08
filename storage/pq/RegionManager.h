#pragma once
#include "MappedRegion.h"
#include "IProductStore.h"
#include <memory>
#include <vector>
#include <sys/types.h>

namespace rdm {
class RegionMapper;
namespace RegionFlags {
constexpr int NoLock   = 0x1;
constexpr int NoWait   = 0x2;
constexpr int Write    = 0x4;
constexpr int Modified = Write;
}

struct ActiveRegion {
  off_t  offset;
  size_t extent;
  void * vp;
  int    rflags;
};

class RegionManager {
private:
  int fd_;
  int pflags_;
  size_t pagesz_;

  std::vector<ActiveRegion> activeRegions_;

  std::unique_ptr<RegionMapper> mapperStrategy_;

  int
  lockKernelRegion(off_t offset, size_t extent, int rflags);
  int
  unlockKernelRegion(off_t offset, size_t extent, int rflags);

public:
  RegionManager(int fd, int pflags, size_t pagesz);
  ~RegionManager();

  RegionManager(const RegionManager&) = delete;
  RegionManager&
  operator = (const RegionManager&) = delete;

  std::unique_ptr<MappedRegion>
  getRegion(off_t offset, size_t extent, int rflags);

  void
  releaseRegion(off_t offset, size_t extent, int rflags, void * ptr);

  int
  getFd() const { return fd_; }

  int
  getFlags() const { return pflags_; }

  size_t
  getPageSize() const { return pagesz_; }
};
}
