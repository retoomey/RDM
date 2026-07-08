#pragma once

#include <cstddef>
#include "RegionList.h"
#include "TimeQueue.h"
#include "FreeBlock.h"
#include "SignatureIndex.h"

namespace rdm {
size_t
ix_sz(size_t nelems, size_t align);

int
ix_ptrs(
  void *                     ix,
  size_t                     ixsz,
  size_t                     nelems,
  size_t                     align,
  rdm::RegionList **     rlpp,
  rdm::TimeQueue **      tqpp,
  rdm::FreeBlock **      fbpp,
  rdm::SignatureIndex ** sxpp);
}
