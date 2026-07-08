#pragma once

#include <sys/types.h>
#include <cstddef>
#include <type_traits>

namespace rdm {
constexpr size_t FB_MAGIC   = 0x54514642;
constexpr int FB_MAX_LEVELS = 15;
constexpr size_t FBLKS_NALLOC_INITIAL = 2;

using fblk_t = size_t;
constexpr fblk_t FBLK_NONE = static_cast<fblk_t>(-1);

class FreeBlock {
private:
  size_t magic_;
  int maxsize_;
  size_t arena_sz_;
  size_t avail_;
  size_t allocated_;
  size_t nfree_[FB_MAX_LEVELS];
  fblk_t free_[FB_MAX_LEVELS];
  fblk_t fblks_[FBLKS_NALLOC_INITIAL];

  void
  InitLevel(fblk_t * offset, int level, int blksize, int numblks);

public:
  static size_t
  GetArenaSize(size_t nelems);
  static size_t
  GetRequiredSize(size_t nelems);
  void
  Initialize(size_t nalloc);

  fblk_t
  Get(int level);
  void
  Release(int size, fblk_t fblk);
  int
  GetRandomLevel() const;

  void
  DumpStats() const;
  size_t
  GetMagic() const { return magic_; }

  int
  GetMaxSize() const { return maxsize_; }

  fblk_t&
  operator [] (size_t index){ return fblks_[index]; }

  const fblk_t&
  operator [] (size_t index) const { return fblks_[index]; }
};

static_assert(std::is_standard_layout_v<FreeBlock>,
  "FreeBlock must remain standard-layout for mmap compatibility");
}
