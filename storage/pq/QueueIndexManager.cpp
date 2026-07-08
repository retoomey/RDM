#include "QueueIndexManager.h"
#include "RegionList.h"
#include "TimeQueue.h"
#include "FreeBlock.h"
#include "SignatureIndex.h"
#include "Registry.h"
#include "IndexLayer.h"
#include "QueueMetrics.h"
#include "FileUtil.h"
#include "Log.h"
#include <cerrno>

namespace rdm {

void QueueIndexManager::InitializeControlHeader(void* ctlRegion, off_t dataOffset, off_t indexOffset,
                                                size_t indexSize, size_t maxSlots, size_t align) {
    pqctl* ctl = static_cast<pqctl*>(ctlRegion);
    ctl->magic = PQ_MAGIC;
    ctl->version = PQ_VERSION;
    ctl->write_count_magic = WRITE_COUNT_MAGIC;
    ctl->write_count = 1;
    ctl->datao = dataOffset;
    ctl->ixo = indexOffset;
    ctl->ixsz = indexSize;
    ctl->nalloc = maxSlots;
    ctl->highwater = 0;
    ctl->maxproducts = 0;
    ctl->align = align;
    ctl->metrics_magic = METRICS_MAGIC;
    ctl->mostRecent = Timestamp::NONE.ToTimeval();
    ctl->minVirtResTime = Timestamp::NONE.ToTimeval();
    ctl->isFull = 0;
    ctl->metrics_magic_2 = METRICS_MAGIC_2;
    ctl->mvrtSize = -1;
    ctl->mvrtSlots = 0;
}

int QueueIndexManager::ValidateControlHeader(const void* ctlRegion, size_t regionSize, size_t pageSize,
                                             off_t& dataOffset, off_t& indexOffset, size_t& indexSize,
                                             size_t& maxSlots, size_t& align) {
    const pqctl* ctl = static_cast<const pqctl*>(ctlRegion);
    
    if (ctl->magic != PQ_MAGIC) {
        LogError("Not a product queue (bad magic number)");
        return EINVAL;
    }
    if (ctl->version != PQ_VERSION && ctl->version != 7) {
        LogError("Product queue is version {} instead of expected version {}", ctl->version, PQ_VERSION);
        return EINVAL;
    }
    if (ctl->datao % pageSize != 0) {
        LogError("Queue data offset not page-aligned");
        return EINVAL;
    }
    if (static_cast<size_t>(ctl->datao) != regionSize) {
        if (regionSize != pageSize) return EINVAL; // Only allow expansion from a base page size probe
        dataOffset = ctl->datao; // Let it know how large to make the new mapping
        return EAGAIN; // Signal to ProductQueue to remap with a larger size
    }
    
    dataOffset = ctl->datao;
    indexOffset = ctl->ixo;
    indexSize = ctl->ixsz;
    maxSlots = ctl->nalloc;
    align = ctl->align;
    return 0;
}

int QueueIndexManager::LayoutExisting(pqctl* ctl, void* indexRegion, size_t indexSize, size_t nalloc, size_t align) {
    if (!indexRegion) return EINVAL;

    ctlp_ = ctl;
    ixp_ = indexRegion;
    ixsz_ = indexSize;
    nalloc_ = nalloc;

    if (!ix_ptrs(ixp_, ixsz_, nalloc_, align, &rlp_, &tqp_, &fbp_, &sxp_)) {
        LogError("Index memory layout boundary check failed. Queue is corrupt.");
        Clear();
        return static_cast<int>(PqStatus::Corrupt);
    }
    return 0;
}

int QueueIndexManager::FormatNew(pqctl* ctl, void* indexRegion, size_t indexSize, size_t nalloc, size_t align, off_t dataOffset, off_t indexOffset) {
    if (!indexRegion) return EINVAL;

    int status = LayoutExisting(ctl, indexRegion, indexSize, nalloc, align);
    if (status != 0) return status;

    fbp_->Initialize(nalloc_);
    tqp_->Initialize(nalloc_, fbp_);
    rlp_->Initialize(nalloc_, fbp_);

    off_t dataSectionSize = indexOffset - dataOffset;
    size_t initialExtent = static_cast<size_t>(dataSectionSize);
    
    if (rlp_->Add(dataOffset, initialExtent) == nullptr) {
        LogError("Failed to initialize the root free-space region.");
        return ENOMEM;
    }
    rlp_->SetInitialMaxFreeExtent(initialExtent);
    sxp_->Initialize(nalloc_);

    return 0;
}

void QueueIndexManager::Clear() {
    ctlp_ = nullptr;
    rlp_ = nullptr;
    tqp_ = nullptr;
    sxp_ = nullptr;
    fbp_ = nullptr;
    ixp_ = nullptr;
    ixsz_ = 0;
    nalloc_ = 0;
    smallest_extent_seen_ = static_cast<size_t>(-1);
}

int QueueIndexManager::TryDeleteProduct(TimeQueueElement* tqep, size_t rlix, ProdInfo& info, const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc) {
    Region* rep = &(*rlp_)[rlix];
    if (rep->offset != tqep->offset) return static_cast<int>(PqStatus::Corrupt);

    int status = decodeFunc(rep->offset, rep->GetExtent(), info);
    if (status != 0) return status; // Fails if region is locked or corrupt

    if (!sxp_->Remove(info.signature)) return static_cast<int>(PqStatus::Corrupt);

    tqp_->Delete(tqep);
    rlp_->Free(rlix);
    
    return 0; 
}

int QueueIndexManager::DeleteOldest(const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc, const std::function<void(const struct timeval*, const ProdInfo&)>& mvrtFunc) {
    int status = EACCES;
    size_t rlix;
    size_t numLocked = 0;

    for (TimeQueueElement* tqep = tqp_->First(); tqep && (rlix = rlp_->Find(tqep->offset)) != static_cast<size_t>(-1); tqep = tqp_->Next(tqep)) {
        ProdInfo info;
        struct timeval insertionTime = tqep->tv;
        status = TryDeleteProduct(tqep, rlix, info, decodeFunc);

        if (status == 0) {
            ctlp_->isFull = 1;
            mvrtFunc(&insertionTime, info);
            return 0;
        }

        if (status != EACCES) return status;
        ++numLocked;
    }

    LogError("All %zu products are locked. No unlocked products left to delete!", numLocked);
    return status;
}

int QueueIndexManager::MakeSpace(size_t extent, size_t& outRlix, const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc, const std::function<void(const struct timeval*, const ProdInfo&)>& mvrtFunc) {
    size_t rlix;
    LogDebug("{}:mkSpace(): Deleting oldest to make space for {} bytes", __FILE__, static_cast<long>(extent));

    do {
        if (rlp_->GetNumElements() == 0) return ENOMEM;
        int status = DeleteOldest(decodeFunc, mvrtFunc);
        if (status != 0) return status;
        rlix = rlp_->Get(extent);
    } while (rlix == static_cast<size_t>(-1));

    outRlix = rlix;
    return 0;
}

int QueueIndexManager::MakeSlot(const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc, const std::function<void(const struct timeval*, const ProdInfo&)>& mvrtFunc) {
    do {
        if (rlp_->GetNumElements() == 0) return ENOMEM;
        int status = DeleteOldest(decodeFunc, mvrtFunc);
        if (status != 0) return status;
    } while (!rlp_->HasSpace());
    return 0;
}

int QueueIndexManager::FreeByOffset(off_t offset, const Signature& sig) {
    size_t rlix = rlp_->Find(offset);
    if (rlix == static_cast<size_t>(-1)) {
        LogError("offset {:#010x}: Not Found", offset);
        return EINVAL;
    }

    Region* rp = &(*rlp_)[rlix];
    if (rp->IsFree()) {
        LogError("offset {:#010x}: Already Free", offset);
        return EINVAL;
    }

    if (!sxp_->Remove(sig)) {
        LogError("signature {}: Not Found", sig.ToString());
        return EINVAL;
    }

    rlp_->Free(rlix);
    return 0;
}

int QueueIndexManager::AllocateProductSpace(size_t extent, const Signature& sig, off_t& outOffset, size_t& outAllocatedExtent, const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc, const std::function<void(const struct timeval*, const ProdInfo&)>& mvrtFunc) {
    SignatureElement* sxep = nullptr;
    if (sxp_->Find(sig, &sxep)) return static_cast<int>(PqStatus::Dup);
    
    if (!rlp_->HasSpace()) {
        int status = MakeSlot(decodeFunc, mvrtFunc);
        if (status != 0) return status;
    }

    extent = roundUp(extent, ctlp_->align);
    if (extent < smallest_extent_seen_) smallest_extent_seen_ = extent;
    
    size_t rlix = rlp_->Get(extent);
    if (rlix == static_cast<size_t>(-1)) {
        int status = MakeSpace(extent, rlix, decodeFunc, mvrtFunc);
        if (status != 0) return status;
    }

    Region* hit = &(*rlp_)[rlix];
    if (extent + smallest_extent_seen_ + 64 < hit->GetExtent()) {
        int status = rlp_->Split(rlix, extent);
        if (status != 0) {
            rlp_->Put(rlix);
            return status;
        }
    }

    hit->SetAlloc();
    rlp_->AddToHash(rlix);

    sxep = sxp_->Add(sig, hit->offset);
    if (!sxep) {
        rlp_->RemoveFromHash(rlix);
        hit->ClearAlloc();
        rlp_->Put(rlix);
        return ENOMEM;
    }

    outOffset = hit->offset;
    outAllocatedExtent = hit->GetExtent();
    return 0;
}

int QueueIndexManager::FindTimeEntryBySignature(const Signature& sig, TimeQueueElement** outTqep, const std::function<int(off_t, size_t, ProdInfo&)>& decodeFunc) {
    SignatureElement* signatureEntry;

    if (!sxp_->Find(sig, &signatureEntry)) return static_cast<int>(PqStatus::NotFound);

    size_t rlix = rlp_->Find(signatureEntry->offset);
    if (rlix == static_cast<size_t>(-1)) {
        LogError("data-product region of signature-map entry doesn't exist");
        return static_cast<int>(PqStatus::Corrupt);
    }
    
    Region* rp = &(*rlp_)[rlix];
    ProdInfo info;
    int status = decodeFunc(rp->offset, rp->GetExtent(), info);
    if (status != 0) return static_cast<int>(PqStatus::Corrupt);

    struct timeval searchTime = info.arrival.ToTimeval();
    searchTime.tv_sec -= registry::getSystemInterval();

    TimeQueueElement* timeEntry = tqp_->Find(&searchTime, Match::LessThan);
    if (!timeEntry) timeEntry = tqp_->Find(&searchTime, Match::Equal);
    if (!timeEntry) timeEntry = tqp_->Find(&searchTime, Match::GreaterThan);

    if (!timeEntry) {
        LogError("The product-queue appears to be empty");
        return static_cast<int>(PqStatus::Corrupt);
    } 
    
    const TimeQueueElement* initialTimeEntry = timeEntry;
    for (;;) {
        if (timeEntry->offset == static_cast<off_t>(-1)) return static_cast<int>(PqStatus::NotFound);
        if (timeEntry->offset == signatureEntry->offset) {
            *outTqep = timeEntry;
            return 0;
        }
        timeEntry = tqp_->Next(timeEntry);
    }

    // Wrap around fallback
    struct timeval tz = Timestamp::ZERO.ToTimeval();
    timeEntry = tqp_->Find(&tz, Match::GreaterThan);
    for (;;) {
        if (initialTimeEntry == timeEntry) break;
        if (timeEntry->offset == signatureEntry->offset) {
            *outTqep = timeEntry;
            return 0;
        }
        timeEntry = tqp_->Next(timeEntry);
    }

    return static_cast<int>(PqStatus::NotFound);
}

}
