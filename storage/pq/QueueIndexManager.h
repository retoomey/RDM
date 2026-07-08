#pragma once

#include "IProductStore.h"
#include "Signature.h"
#include "ProdInfo.h"

#include <functional>

#include <sys/types.h>

namespace rdm {

constexpr size_t PQ_MAGIC   = 0x50515545;
constexpr size_t PQ_VERSION = 7;
constexpr unsigned WRITE_COUNT_MAGIC = 0x50515545;
constexpr unsigned MAX_WRITE_COUNT   = ~0u;

struct TimeQueueElement;
class RegionList;
class TimeQueue;
class FreeBlock;
class SignatureIndex;
}

int
ix_ptrs(void * ix, size_t ixsz, size_t nelems, size_t align,
  rdm::RegionList ** rlpp, rdm::TimeQueue ** tqpp,
  rdm::FreeBlock ** fbpp, rdm::SignatureIndex ** sxpp);

struct pqctl {
  size_t         magic;
  size_t         version;
  off_t          datao;
  off_t          ixo;
  size_t         ixsz;
  size_t         nalloc;
  size_t         align;
  off_t          highwater;
  size_t         maxproducts;
  unsigned       write_count_magic;
  unsigned       write_count;
  unsigned       metrics_magic;
  struct timeval mostRecent;
  struct timeval minVirtResTime;
  int            isFull;
  unsigned       metrics_magic_2;
  off_t          mvrtSize;
  size_t         mvrtSlots;
};

namespace rdm {
class QueueIndexManager {
private:
  pqctl * ctlp_{ nullptr };
  RegionList * rlp_{ nullptr };
  TimeQueue * tqp_{ nullptr };
  SignatureIndex * sxp_{ nullptr };
  FreeBlock * fbp_{ nullptr };

  void * ixp_{ nullptr };
  size_t ixsz_{ 0 };
  size_t nalloc_{ 0 };

  size_t smallest_extent_seen_{ static_cast<size_t>(-1) };

  int
  MakeSpace(size_t extent, size_t& outRlix, const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc,
    const std::function<void(const struct timeval *, const ProdInfo&)>& mvrtFunc);
  int
  MakeSlot(const std::function<int(off_t, size_t, ProdInfo&)>         & decodeFunc,
    const std::function<void(const struct timeval *, const ProdInfo&)>& mvrtFunc);
  int
  DeleteOldest(const std::function<int(off_t, size_t, ProdInfo&)>     & decodeFunc,
    const std::function<void(const struct timeval *, const ProdInfo&)>& mvrtFunc);
  int
  TryDeleteProduct(TimeQueueElement * tqep, size_t rlix, ProdInfo& info, const std::function<int(off_t, size_t,
    ProdInfo&)>& decodeFunc);

public:
  QueueIndexManager()  = default;
  ~QueueIndexManager() = default;

  QueueIndexManager(const QueueIndexManager&) = delete;
  QueueIndexManager&
  operator = (const QueueIndexManager&) = delete;

  static void
  InitializeControlHeader(void* ctlRegion, off_t dataOffset, off_t indexOffset,
                          size_t indexSize, size_t maxSlots, size_t align);

  static int
  ValidateControlHeader(const void* ctlRegion, size_t regionSize, size_t pageSize,
                        off_t& dataOffset, off_t& indexOffset, size_t& indexSize,
                        size_t& maxSlots, size_t& align);

  int
  LayoutExisting(pqctl * ctl, void * indexRegion, size_t indexSize, size_t nalloc, size_t align);
  int
  FormatNew(pqctl * ctl, void * indexRegion, size_t indexSize, size_t nalloc, size_t align, off_t dataOffset,
    off_t indexOffset);
  void
  Clear();

  int
  AllocateProductSpace(size_t extent, const Signature& sig, off_t& outOffset, size_t& outAllocatedExtent,
    const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc,
    const std::function<void(const struct timeval *, const ProdInfo&)>& mvrtFunc);

  int
  FreeByOffset(off_t offset, const Signature& sig);

  int
  FindTimeEntryBySignature(const Signature& sig, TimeQueueElement ** outTqep,
    const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc);


  pqctl *
  GetControl() const { return ctlp_; }

  RegionList *
  GetRegionList() const { return rlp_; }

  TimeQueue *
  GetTimeQueue() const { return tqp_; }

  SignatureIndex *
  GetSignatureIndex() const { return sxp_; }

  FreeBlock *
  GetFreeBlock() const { return fbp_; }
};
}
