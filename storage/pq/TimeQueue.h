#pragma once
#include <sys/types.h>
#include <sys/time.h>
#include <cstddef>
#include <type_traits>
#include "IProductStore.h"
#include "FreeBlock.h"
#include "Timestamp.h"

namespace rdm {
constexpr size_t TQ_OVERHEAD_ELEMS = 2;
constexpr size_t TQ_NALLOC_INITIAL = 84;
using tqep_t = off_t;
constexpr tqep_t TQ_NONE = static_cast<tqep_t>(-1);
constexpr tqep_t TQ_NIL  = static_cast<tqep_t>(0);
constexpr tqep_t TQ_HEAD = static_cast<tqep_t>(1);

struct TimeQueueElement {
  struct timeval tv;
  off_t          offset;
  fblk_t         fblk;
};

class TimeQueue {
private:
  size_t nalloc_;
  size_t nelems_;
  size_t nfree_;
  tqep_t free_head_;
  int level_;
  off_t fbp_off_;
  TimeQueueElement tqep_[TQ_NALLOC_INITIAL];

  FreeBlock *
  GetFreeBlock() const;
  tqep_t
  AllocateElement();
  void
  FreeElement(int level, tqep_t p);

public:
  static size_t
  GetRequiredSize(size_t nelems);
  void
  Initialize(size_t allocSize, FreeBlock * fbp);

  bool
  HasSpace() const;
  size_t
  GetAllocSize() const { return nalloc_; }

  int
  Add(off_t offset);
  TimeQueueElement *
  Find(const struct timeval * key, Match mt) const;
  TimeQueueElement *
  First() const;
  TimeQueueElement *
  Next(const TimeQueueElement * tqep) const;
  void
  Delete(TimeQueueElement * tqep);
};

// Guarantee the C++ class maintains strict memory compatibility with legacy C 'mmap' layouts.
static_assert(std::is_standard_layout_v<TimeQueue>,
  "TimeQueue must remain standard-layout for mmap compatibility");
}
