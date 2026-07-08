#pragma once
#include "QueueIndexManager.h"
#include "RegionList.h"
#include "TimeQueue.h"
#include "Timestamp.h"
#include "Log.h"

namespace rdm {
constexpr unsigned METRICS_MAGIC   = 0x50515546;
constexpr unsigned METRICS_MAGIC_2 = 0x50515547;

class QueueMetrics {
private:
  QueueIndexManager& indexMgr_;

public:
  explicit QueueMetrics(QueueIndexManager& mgr) : indexMgr_(mgr){ }

  void
  TrackAllocation(off_t offset, size_t allocatedExtent)
  {
    pqctl * ctl     = indexMgr_.GetControl();
    RegionList * rl = indexMgr_.GetRegionList();

    if (!ctl || !rl) { return; }

    off_t highwater = offset + static_cast<off_t>(allocatedExtent) - ctl->datao;

    if (highwater > ctl->highwater) {
      ctl->highwater = highwater;
    }

    if (rl->GetNumElements() > ctl->maxproducts) {
      ctl->maxproducts = rl->GetNumElements();
    }

    rl->nbytes_ += allocatedExtent;
    if (rl->nbytes_ > rl->maxbytes_) {
      rl->maxbytes_ = rl->nbytes_;
    }
  }

  void
  TrackMvrt(const Timestamp& creationTime, const Timestamp& receptionTime)
  {
    pqctl * ctl     = indexMgr_.GetControl();
    RegionList * rl = indexMgr_.GetRegionList();

    if (!ctl || !rl) { return; }

    Timestamp creatPtr = (receptionTime < creationTime) ? receptionTime : creationTime;
    Timestamp now      = Timestamp::Now();

    if (now > creatPtr) {
      Timestamp virtResTime = now - creatPtr;
      Timestamp currentMin  = Timestamp::FromTimeval(ctl->minVirtResTime);

      if ((currentMin == Timestamp::NONE) || (virtResTime < currentMin) ) {
        ctl->minVirtResTime = virtResTime.ToTimeval();
        ctl->mvrtSize       = rl->GetNumBytes();
        ctl->mvrtSlots      = rl->GetNumElements();
      }
    }
  }

  void
  MarkMostRecent()
  {
    pqctl * ctl = indexMgr_.GetControl();

    if (ctl) { ctl->mostRecent = Timestamp::Now().ToTimeval(); }
  }

  void
  ClearMvrt()
  {
    pqctl * ctl = indexMgr_.GetControl();

    if (!ctl) { return; }
    ctl->minVirtResTime = Timestamp::NONE.ToTimeval();
    ctl->mvrtSize       = -1;
    ctl->mvrtSlots      = 0;
  }

  void
  GetMvrt(Timestamp& minVirtResTime, off_t& size, size_t& slots) const
  {
    pqctl * ctl = indexMgr_.GetControl();

    if (!ctl) { return; }

    if (METRICS_MAGIC == ctl->metrics_magic) {
      minVirtResTime = Timestamp::FromTimeval(ctl->minVirtResTime);
    } else {
      minVirtResTime = Timestamp::NONE;
    }

    if (METRICS_MAGIC_2 == ctl->metrics_magic_2) {
      size  = ctl->mvrtSize;
      slots = ctl->mvrtSlots;
    } else {
      size  = -1;
      slots = 0;
    }
  }

  void
  GetStats(size_t& nprods, size_t& nfree, size_t& nempty, size_t& nbytes,
    size_t& maxprods, size_t& maxfree, size_t& minempty, size_t& maxbytes,
    double& age_oldest, size_t& maxextent) const
  {
    RegionList * rl = indexMgr_.GetRegionList();
    TimeQueue * tq  = indexMgr_.GetTimeQueue();

    if (!rl || !tq) { return; }

    nprods    = rl->GetNumElements();
    nfree     = rl->GetNumFree();
    maxextent = rl->GetMaxFreeExtent();
    nempty    = rl->GetNumEmpty();
    nbytes    = rl->GetNumBytes();
    maxprods  = rl->GetMaxElements();
    maxfree   = rl->GetMaxFree();
    minempty  = rl->GetMinEmpty();
    maxbytes  = rl->GetMaxBytes();

    TimeQueueElement * tqep = tq->First();

    if (tqep != nullptr) {
      Timestamp now    = Timestamp::Now();
      Timestamp oldest = Timestamp::FromTimeval(tqep->tv);
      age_oldest = (now - oldest).AsSeconds();
    } else {
      age_oldest = 0;
    }
  }
};
}
