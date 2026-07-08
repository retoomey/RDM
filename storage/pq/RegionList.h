#pragma once

#include <sys/types.h>
#include <cstddef>
#include <climits>
#include <type_traits>
#include "FreeBlock.h"


namespace rdm {

class ProductQueue;

constexpr size_t RL_NONE  = static_cast<size_t>(-1);
constexpr size_t RL_MAGIC = 0x524c4841;
constexpr size_t RL_NALLOC_INITIAL = 5;
constexpr size_t RL_FREE_OVERHEAD  = 4;
constexpr size_t RL_EMPTY_HD       = RL_FREE_OVERHEAD;
constexpr size_t RL_EXP_CHAIN_LEN  = 4;

constexpr size_t RL_FOFF_HD = 0;
constexpr size_t RL_FOFF_TL = 1;
constexpr size_t RL_FEXT_HD = 2;
constexpr size_t RL_FEXT_TL = 3;

struct Region {
  off_t  offset;
  size_t extent;
  size_t next;
  size_t prev;

  bool
  IsAlloc() const { return (extent & 0x1) != 0; }

  bool
  IsFree() const { return !IsAlloc(); }

  size_t
  GetExtent() const { return extent & ~static_cast<size_t>(0x1); }

  void SetAlloc(){ extent |= 0x1; }

  void ClearAlloc(){ extent &= ~static_cast<size_t>(0x1); }
};

class RegionList {
  friend class ProductQueue;
  friend class QueueMetrics;
private:
  size_t nalloc_;
  size_t nchains_;
  size_t empty_;
  size_t nelems_;
  size_t maxelems_;
  size_t nfree_;
  size_t maxfree_;
  size_t maxfextent_;
  size_t nempty_;
  size_t minempty_;
  size_t nbytes_;
  size_t maxbytes_;
  off_t fbp_off_;
  size_t level_foff_;
  size_t foff_;
  size_t level_fext_;
  size_t fext_;
  Region rp_[RL_NALLOC_INITIAL];

  struct RegionHash {
    size_t magic;
    size_t chains[1];
  };

  RegionHash *
  GetHashTable();
  const RegionHash *
  GetHashTable() const;
  FreeBlock *
  GetFreeBlock() const;

  void
  InitArray();
  size_t
  GetEmptyRegion();
  void
  ReleaseEmptyRegion(size_t rlix);
  size_t
  HashOffset(off_t offset) const;
  void
  InitHash();
  void
  InitOffsets();
  void
  InitExtents();

  void
  SearchOffset(off_t targetOffset, size_t * update, Region ** outSqp) const;
  void
  SearchExtent(size_t targetExtent, off_t targetOffset, size_t * update, Region ** outSqp) const;

  size_t
  FindPrevExtent(size_t rlix);
  size_t
  RecalculateMaxFreeExtent();
  void
  DeleteOffset(size_t rlix);
  void
  DeleteExtent(size_t rlix);
  size_t
  FindExtent(size_t extent);
  int
  AddOffset(size_t rlix);
  int
  AddExtent(size_t rlix);
  int
  ReleaseRegion(size_t rlix);

  size_t
  GetNextOffset(size_t rlix);
  size_t
  GetPrevOffset(size_t rlix);
  void
  Consolidate(size_t rpix);

public:
  static size_t
  GetRequiredSize(size_t nelems);
  void
  Initialize(size_t nalloc, FreeBlock * fbp);

  size_t
  Find(off_t offset) const;
  bool
  Find(off_t offset, Region ** rpp);
  size_t
  Get(size_t extent);
  void
  Put(size_t rlix);
  void
  Free(size_t rpix);
  int
  Split(size_t rlix, size_t extent);
  bool
  HasSpace() const;
  Region *
  Add(off_t offset, size_t extent);

  void
  AddToHash(size_t rpix);
  void
  RemoveFromHash(size_t rlix);
  void SetInitialMaxFreeExtent(size_t extent){ maxfextent_ = extent; }

  size_t
  GetAllocSize() const { return nalloc_; }

  size_t
  GetNumElements() const { return nelems_; }

  size_t
  GetNumFree() const { return nfree_; }

  size_t
  GetNumEmpty() const { return nempty_; }

  size_t
  GetNumBytes() const { return nbytes_; }

  size_t
  GetMaxElements() const { return maxelems_; }

  size_t
  GetMaxFree() const { return maxfree_; }

  size_t
  GetMinEmpty() const { return minempty_; }

  size_t
  GetMaxBytes() const { return maxbytes_; }

  size_t
  GetMaxFreeExtent() const { return maxfextent_; }

  Region&
  operator [] (size_t index){ return rp_[index]; }

  const Region&
  operator [] (size_t index) const { return rp_[index]; }
};

static_assert(std::is_standard_layout_v<RegionList>,
  "RegionList must remain standard-layout for mmap compatibility");
}
