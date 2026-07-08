#include "config.h"
#include "Log.h"
#include "Registry.h"
#include "Product.h"
#include "ProductQueue.h"
#include "QueueCursor.h"
#include "ProcessUtil.h"
#include "Pattern.h"
#include "BitUtil.h"
#include "FreeBlock.h"
#include "TimeQueue.h"
#include "RegionList.h"
#include "SignatureIndex.h"
#include "FileUtil.h"
#include "IndexLayer.h"
#include "Timestamp.h"
#include "Signature.h"
#include "TimeQueue.h"
#include "FreeBlock.h"
#include "SignatureIndex.h"
#include "MappedRegion.h"
#include <cstring>
#include <numeric>
#include <signal.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <search.h>
#include <stdint.h>
#include <fcntl.h>

#define OFF_NONE  ((off_t)(-1))

namespace rdm {

long ProductQueue::getPQECount()
{
    return pqe_count_.load(std::memory_order_relaxed);
}

ProductQueue::ProductQueue(std::shared_ptr<IProductSerializer> serializer)
    : serializer_(std::move(serializer))
{
}

ProductQueue::ProductQueue(const std::string& path, const int flags, std::shared_ptr<IProductSerializer> serializer)
    : serializer_(std::move(serializer))
{
    if (!serializer_) {
        LogFatal("ProductQueue requires a valid IProductSerializer");
        exit(1);
    }
    (void)open(path, flags);
}

ProductQueue::~ProductQueue() {
    close();
}

bool ProductQueue::isProductMappingNecessary() const
{
   return (static_cast<off_t>(indexOffset_ + indexSize_) > MAX_SIZE_T);
}

int ProductQueue::initQueue(int pflags, size_t align, off_t initialsz, size_t maxProds) {
    pflags_ = pflags;
    fSet(pflags_, PqFlags::NoGrow);

    // Use file_.pageSize() directly for the alignment math
    dataOffset_ = std::lcm(file_.pageSize(), align);
    indexOffset_ = dataOffset_ + roundUp(initialsz, file_.pageSize());
    maxSlots_ = maxProds;
    if (maxProds == 0) {
       indexSize_ = file_.pageSize();
    } else {
       indexSize_ = roundUp(ix_sz(maxProds, align), file_.pageSize());
    }
    if (indexOffset_ < dataOffset_){
       return EINVAL;
    }

    mutex_ = std::make_unique<PqMutex>(fIsSet(pflags_, PqFlags::ThreadSafe));
    return 0;
}

int ProductQueue::create(const std::string& path, mode_t mode, int pflags, size_t align,
                         off_t initialsz, size_t nproducts) {
    close();
    align = (align == 0 ? M_RND_UNIT : roundUp(align, M_RND_UNIT));
    initialsz = (initialsz != 0) ? static_cast<off_t>(roundUp(initialsz, align)) : static_cast<off_t>(align);
    int initStatus = initQueue(pflags, align, initialsz, nproducts);
    if (initStatus != 0) return initStatus;

    // RAII File Creation
    int fileStatus = file_.create(path, mode, pflags_);
    if (fileStatus != 0) return fileStatus;

    int growStatus = fgrow(file_.fd(), total_size(), fIsSet(pflags_, PqFlags::Sparse));
    if (growStatus != 0) {
        LogError("Failed to physically allocate queue file bounds: {}", std::strerror(growStatus));
        file_.unlink();
        return growStatus;
    }
    regionManager_ = std::make_unique<RegionManager>(file_.fd(), pflags_, file_.pageSize());

    int status = formatControlRegion(align);
    if (status != 0) {
        file_.unlink();
        return status;
    }

    if (idxRegion_) {
        idxRegion_->markModified();
        idxRegion_.reset();
    }
    if (ctlRegion_) {
        ctlRegion_->markModified();
        ctlRegion_.reset();
    }
    indexManager_.Clear();

    return 0;
}

int ProductQueue::open(const std::string& path, const int flags) {
    close();
    int status = initQueue(flags, M_RND_UNIT, 0, 0);
    if (status != 0) return status;

    // RAII File Open
    int fileStatus = file_.open(path, fIsSet(flags, PqFlags::ReadOnly));
    if (fileStatus != 0) return fileStatus;

    regionManager_ = std::make_unique<RegionManager>(file_.fd(), pflags_, file_.pageSize());

    status = loadControlRegion(path);
    if (status == 0) {
        idxRegion_.reset();
        ctlRegion_.reset();
        indexManager_.Clear();

        if (!fIsSet(flags, PqFlags::ReadOnly)) {
            ControlLock clock(*this, RegionFlags::Write);
            if (clock) {
                if (WRITE_COUNT_MAGIC != indexManager_.GetControl()->write_count_magic) {
                    indexManager_.GetControl()->write_count_magic = WRITE_COUNT_MAGIC;
                    indexManager_.GetControl()->write_count = 0;
                }
                if (MAX_WRITE_COUNT > indexManager_.GetControl()->write_count) {
                    indexManager_.GetControl()->write_count++;
                } else {
                    LogError("Too many writers ({}) to product-queue ({})", indexManager_.GetControl()->write_count, path);
                    status = EACCES;
                }

                if (status == 0) {
                    if (METRICS_MAGIC != indexManager_.GetControl()->metrics_magic) {
                        indexManager_.GetControl()->metrics_magic = METRICS_MAGIC;
                        indexManager_.GetControl()->mostRecent = Timestamp::NONE.ToTimeval();
                        indexManager_.GetControl()->minVirtResTime = Timestamp::NONE.ToTimeval();
                        indexManager_.GetControl()->isFull = 0;
                    }
                    if (METRICS_MAGIC_2 != indexManager_.GetControl()->metrics_magic_2) {
                        indexManager_.GetControl()->metrics_magic_2 = METRICS_MAGIC_2;
                        indexManager_.GetControl()->mvrtSize = -1;
                        indexManager_.GetControl()->mvrtSlots = 0;
                    }
                }
            } else {
                status = clock.status();
            }
        }
    }

    if (status != 0) {
        file_.close();
    }
    return status;
}

int ProductQueue::close() {
    int status = 0;
    if (!file_.isOpen()) return 0;
    
    activeUserRegions_.clear();
    
    if (!fIsSet(pflags_, PqFlags::ReadOnly)) {
        {
            ControlLock clock(*this, RegionFlags::Write);
            if (clock && indexManager_.GetControl()->write_count > 0) {
                indexManager_.GetControl()->write_count--;
            }
        } 
    }

    if (file_.close() != 0 && !status) {
        status = errno;
    }
    return status;
}

int ProductQueue::formatControlRegion(size_t const align) {
    int status = 0;
    ctlRegion_ = regionManager_->getRegion(0, static_cast<size_t>(dataOffset_), RegionFlags::Write | RegionFlags::NoWait);
    if (!ctlRegion_ && !fIsSet(pflags_, PqFlags::NoMap)) {
        LogNotice("EIO => remote file system. Falling back to NoMap strategy.");
        fSet(pflags_, PqFlags::NoMap);
        //regionManager_ = std::make_unique<RegionManager>(fd_, pflags_, pagesz_);
        regionManager_ = std::make_unique<RegionManager>(file_.fd(), pflags_, file_.pageSize());
        ctlRegion_ = regionManager_->getRegion(0, static_cast<size_t>(dataOffset_), RegionFlags::Write | RegionFlags::NoWait);
    }
    if (!ctlRegion_) return EIO;

    // Delegate schema initialization entirely to the Index Manager
    QueueIndexManager::InitializeControlHeader(ctlRegion_->get(), dataOffset_, indexOffset_, indexSize_, maxSlots_, align);

    idxRegion_ = regionManager_->getRegion(indexOffset_, indexSize_, RegionFlags::Write | RegionFlags::NoLock);
    if (!idxRegion_) {
        ctlRegion_.reset();
        return EIO;
    }
    
    status = indexManager_.FormatNew(static_cast<pqctl*>(ctlRegion_->get()), idxRegion_->get(), indexSize_, maxSlots_, align, dataOffset_, indexOffset_);
    return status;
}

int ProductQueue::loadControlRegion(const std::string& path) {
    //size_t ctlsz = pagesz_;
    size_t ctlsz = file_.pageSize();
    size_t align = 0;
    std::unique_ptr<MappedRegion> tempCtlRegion;
    
    while (true) {
        tempCtlRegion = regionManager_->getRegion(0, ctlsz, 0);
        if (!tempCtlRegion && !fIsSet(pflags_, PqFlags::NoMap)) {
            LogWarning("Product-queue can't be memory-mapped! Continuing with slower read/write I/O.");
            fSet(pflags_, PqFlags::NoMap);
            //regionManager_ = std::make_unique<RegionManager>(fd_, pflags_, pagesz_);
            regionManager_ = std::make_unique<RegionManager>(file_.fd(), pflags_, file_.pageSize());
            tempCtlRegion = regionManager_->getRegion(0, ctlsz, 0);
        }
        if (!tempCtlRegion) return EIO;

        // Delegate validation to the Index Manager
        //int vStatus = QueueIndexManager::ValidateControlHeader(tempCtlRegion->get(), ctlsz, pagesz_,
        //                                                       dataOffset_, indexOffset_, indexSize_, maxSlots_, align);
        int vStatus = QueueIndexManager::ValidateControlHeader(tempCtlRegion->get(), ctlsz, file_.pageSize(),
                                                               dataOffset_, indexOffset_, indexSize_, maxSlots_, align);
        
        if (vStatus == EAGAIN) {
            ctlsz = static_cast<size_t>(dataOffset_);
            tempCtlRegion.reset();
            continue; // Re-probe the OS with the proper mapped boundaries
        } else if (vStatus != 0) {
            LogError("{}: Invalid control region", path);
            return vStatus;
        }
        break;
    }

    tempCtlRegion.reset();
    idxRegion_ = regionManager_->getRegion(indexOffset_, indexSize_, RegionFlags::NoLock);
    if (!idxRegion_) return EIO;
    
    if (indexManager_.LayoutExisting(nullptr, idxRegion_->get(), indexSize_, maxSlots_, align) != 0) {
        idxRegion_.reset();
        return static_cast<int>(PqStatus::Corrupt);
    }
    return 0;
}

std::string ProductQueue::getPathname() {
   //return pathname_;
   return file_.path();
}

int ProductQueue::getFlags() {
    ThreadLock tlock(*mutex_);
    return pflags_;
}

int ProductQueue::getPageSize() const {
    //if (pagesz_ == 0) return (int)pagesize();
    //ThreadLock tlock(*mutex_);
    //return (int)pagesz_;
    // Do we need to lock this?
    return (int)file_.pageSize();
}

int ProductQueue::getMostRecent(Timestamp& mostRecent) {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, 0);
    if (!clock) return clock.status();
    mostRecent.tv_sec = indexManager_.GetControl()->mostRecent.tv_sec;
    mostRecent.tv_usec = indexManager_.GetControl()->mostRecent.tv_usec;
    return 0;
}

size_t ProductQueue::getDataSize() const {
    ThreadLock tlock(*mutex_);
    return indexOffset_ - dataOffset_;
}

int ProductQueue::insert(const Product& clean_prod) {
    if (fIsSet(pflags_, PqFlags::ReadOnly)) {
        return EACCES;
    }
    
    std::unique_ptr<IQueueEntry> entry;
    int status = newElement(clean_prod.info, entry);
    
    if (status == 0) {
        if (serializer_->EncodeProduct(entry->getWritePointer(), entry->getSize(), clean_prod)) {
            status = entry->commit();
        } else {
            status = EIO;
            entry->rollback();
        }
    }
    return status;
}

int ProductQueue::getOldestCursor(Timestamp& oldestCursor) {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, 0);
    if (!clock) return clock.status();
    TimeQueueElement* tqep = indexManager_.GetTimeQueue()->First();
    if (tqep == nullptr) {
        oldestCursor = Timestamp::NONE;
    } else {
        oldestCursor = Timestamp::FromTimeval(tqep->tv);
    }
    return 0;
}

int ProductQueue::release(const off_t offset) {
    ThreadLock tlock(*mutex_);
    int status = 0;
    auto it = activeUserRegions_.find(offset);
    if (it != activeUserRegions_.end()) {
        activeUserRegions_.erase(it);
        locked_count_--;
    } else {
        status = static_cast<int>(PqStatus::NotFound);
    }
    if (status) LogError("Couldn't release offset {}, status={}", offset, status);
    return status;
}

size_t ProductQueue::getMagic() {
    //return (fd_ == -1 || !indexManager_.GetControl()) ? 0 : indexManager_.GetControl()->magic;
    return (!file_.isOpen() || !indexManager_.GetControl()) ? 0 : indexManager_.GetControl()->magic;
}

int ProductQueue::deleteBySignature(const Signature& signature) {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, RegionFlags::Write);
    if (!clock) {
        LogError("Couldn't lock the control-header of product-queue {}", file_.path());
        return static_cast<int>(PqStatus::System);
    }
    auto decodeFunc = [&](off_t readOffset, size_t readExtent, ProdInfo& outInfo) -> int {
        auto region = regionManager_->getRegion(readOffset, readExtent, RegionFlags::Write | RegionFlags::NoWait);
        if (!region) return EACCES;
        if (!serializer_->DecodeProdInfo(region->get(), readExtent, outInfo)) return static_cast<int>(PqStatus::Corrupt);
        return 0;
    };
    TimeQueueElement* timeEntry;
    int status = indexManager_.FindTimeEntryBySignature(signature, &timeEntry, decodeFunc);
    if (status == 0) {
        size_t rlix = indexManager_.GetRegionList()->Find(timeEntry->offset);
        ProdInfo prodInfo;
        auto region = regionManager_->getRegion(timeEntry->offset, (*indexManager_.GetRegionList())[rlix].GetExtent(), RegionFlags::Write | RegionFlags::NoWait);
        if (!region) {
            status = static_cast<int>(PqStatus::Locked);
        } else if (!serializer_->DecodeProdInfo(region->get(), region->getExtent(), prodInfo)) {
            status = static_cast<int>(PqStatus::Corrupt);
        } else {
            indexManager_.GetSignatureIndex()->Remove(prodInfo.signature);
            indexManager_.GetTimeQueue()->Delete(timeEntry);
            indexManager_.GetRegionList()->Free(rlix);
        }
    }
    return status;
}

const char* ProductQueue::strerror(const int error) const {
    ThreadLock tlock(*mutex_);
    if (0 == error) return "Success";
    if (0 < error) return ::strerror(error);
    switch (error) {
        case static_cast<int>(PqStatus::End):      return "End of product-queue reached";
        case static_cast<int>(PqStatus::NotFound): return "Desired data-product not found";
        case static_cast<int>(PqStatus::Corrupt):  return "Product-queue is corrupt";
        default:                                          return "Unknown error-code";
    }
}

size_t ProductQueue::getSlotCount() const {
    if (!file_.isOpen()) return 0;
    ThreadLock tlock(*mutex_);
    return maxSlots_;
}

size_t ProductQueue::getMagic() const {
    return (!file_.isOpen() || !indexManager_.GetControl()) ? 0 : indexManager_.GetControl()->magic;
}

int ProductQueue::newElement(const ProdInfo& info, std::unique_ptr<IQueueEntry>& outEntry) {
    if (!file_.isOpen()) return EINVAL;
    ThreadLock tlock(*mutex_);
    if (info.sz == 0) return EINVAL;
    if (info.sz > getDataSize()) return static_cast<int>(PqStatus::Big);
    if (fIsSet(pflags_, PqFlags::ReadOnly)) return EACCES;
    ControlLock clock(*this, RegionFlags::Write);
    if (!clock) return clock.status();
    
    Product dummyProd;
    dummyProd.info = info;
    dummyProd.data = nullptr;
    size_t extent = serializer_->GetEncodedSize(dummyProd);
    off_t offset;
    size_t allocatedExtent;
    auto decodeFunc = [&](off_t readOffset, size_t readExtent, ProdInfo& outInfo) -> int {
        auto region = regionManager_->getRegion(readOffset, readExtent, RegionFlags::Write | RegionFlags::NoWait);
        if (!region) return EACCES;
        if (!serializer_->DecodeProdInfo(region->get(), readExtent, outInfo)) return static_cast<int>(PqStatus::Corrupt);
        return 0;
    };
    auto mvrtFunc = [&](const struct timeval* receptionTime, const ProdInfo& mInfo) {
        QueueMetrics(this->indexManager_).TrackMvrt(mInfo.arrival, Timestamp::FromTimeval(*receptionTime));
    };
    
    int status = indexManager_.AllocateProductSpace(extent, info.signature, offset, allocatedExtent, decodeFunc, mvrtFunc);
    if (status != 0) return status;
    
    QueueMetrics(indexManager_).TrackAllocation(offset, allocatedExtent);
    auto region = regionManager_->getRegion(offset, allocatedExtent, RegionFlags::Write);
    if (!region) return ENOMEM;
    
    void* payloadPtr = serializer_->EncodeProdInfo(region->get(), extent, info);
    if (payloadPtr == nullptr) return EIO;
    
    pqe_index idx;
    idx.offset = offset;
    idx.signature = info.signature;
    idx.sig_is_set = true;
    pqe_count_++;
    
    // Change outEntry construction to use std::make_unique
    auto entry = std::make_unique<QueueEntry>(
        std::move(region),
        idx,
        [this](const pqe_index& i, std::unique_ptr<MappedRegion> r) {
            return this->commitEntry(i, std::move(r));
        },
        [this](const pqe_index& i, std::unique_ptr<MappedRegion> r) {
            return this->rollbackEntry(i, std::move(r));
        }
    );
    entry->setPayloadPointer(payloadPtr);
    outEntry = std::move(entry);
    
    return 0;
   
}

int ProductQueue::commitEntry(const pqe_index& index, std::unique_ptr<MappedRegion> region) {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    int status = 0;
    {
        ThreadLock tlock(*mutex_);
        ProdInfo cleanInfo;
        if (!serializer_->DecodeProdInfo(region->get(), region->getExtent(), cleanInfo)) {
            status = static_cast<int>(PqStatus::Corrupt);
        } else if (serializer_->GetEncodedInfoSize(cleanInfo) > region->getExtent()) {
            status = static_cast<int>(PqStatus::Big);
        } else {
            region->markModified();
            region.reset();
            ControlLock clock(*this, RegionFlags::Write);
            if (!clock) {
                status = static_cast<int>(PqStatus::System);
            } else {
                if (indexManager_.GetTimeQueue()->Add(index.offset)) {
                    status = static_cast<int>(PqStatus::System);
                } else {
                    QueueMetrics(indexManager_).MarkMostRecent();
                    pqe_count_--;
                    status = 0;
                }
            }
        }
    }
    if (status) {
        rollbackEntry(index, std::move(region));
    }
    return status;
}

int ProductQueue::rollbackEntry(const pqe_index& index, std::unique_ptr<MappedRegion> region) {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    region.reset();
    ControlLock clock(*this, RegionFlags::Write);
    if (!clock) return clock.status();
    int status = indexManager_.FreeByOffset(index.offset, index.signature);
    if (!status) pqe_count_--;
    return status;
}

int ProductQueue::newElementDirect(const size_t size, const Signature& signature, std::unique_ptr<IQueueEntry>& outEntry) {
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    if (size > getDataSize()) {
        LogError("Product too big: product={} bytes; queue={} bytes",
                  static_cast<unsigned long>(size), static_cast<unsigned long>(getDataSize()));
        return static_cast<int>(PqStatus::Big);
    }
    if (fIsSet(pflags_, PqFlags::ReadOnly)) {
        LogError("Product-queue is read-only");
        return EACCES;
    }
    ControlLock clock(*this, RegionFlags::Write);
    if (!clock) return clock.status();
    
    off_t offset;
    size_t allocatedExtent;
    auto decodeFunc = [&](off_t readOffset, size_t readExtent, ProdInfo& outInfo) -> int {
        auto region = regionManager_->getRegion(readOffset, readExtent, RegionFlags::Write | RegionFlags::NoWait);
        if (!region) return EACCES;
        if (!serializer_->DecodeProdInfo(region->get(), readExtent, outInfo)) return static_cast<int>(PqStatus::Corrupt);
        return 0;
    };
    auto mvrtFunc = [&](const struct timeval* receptionTime, const ProdInfo& mInfo) {
        QueueMetrics(this->indexManager_).TrackMvrt(mInfo.arrival, Timestamp::FromTimeval(*receptionTime));
    };
    
    int status = indexManager_.AllocateProductSpace(size, signature, offset, allocatedExtent, decodeFunc, mvrtFunc);
    if (status != 0) {
        if (status != static_cast<int>(PqStatus::Dup)) {
            LogError("AllocateProductSpace() failure: size: {}", size);
        }
        return status;
    }
    
    QueueMetrics(indexManager_).TrackAllocation(offset, allocatedExtent);
    auto region = regionManager_->getRegion(offset, allocatedExtent, RegionFlags::Write);
    if (!region) {
        indexManager_.FreeByOffset(offset, signature);
        return ENOMEM;
    }
    
    pqe_index idx;
    idx.offset = offset;
    idx.signature = signature;
    idx.sig_is_set = true;
    pqe_count_++;
    
    outEntry = std::make_unique<QueueEntry>(
        std::move(region),
        idx,
        [this](const pqe_index& i, std::unique_ptr<MappedRegion> r) {
            return this->commitEntry(i, std::move(r));
        },
        [this](const pqe_index& i, std::unique_ptr<MappedRegion> r) {
            return this->rollbackEntry(i, std::move(r));
        }
    );
    return 0;
}

int ProductQueue::getHighwater(off_t& highwaterBytes, size_t& maxProducts) {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, 0);
    if (!clock) return clock.status();
    highwaterBytes = indexManager_.GetControl()->highwater;
    maxProducts = indexManager_.GetControl()->maxproducts;
    return 0;
}

bool ProductQueue::isFull() {
    //if (fd_ == -1) return false;
    if (!file_.isOpen()){
      return false;
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, 0);
    if (!clock) return false;
    return (indexManager_.GetControl()->isFull != 0);
}

int ProductQueue::getMinVirtResTimeMetrics(Timestamp& minVirtResTime, off_t& size, size_t& slots) {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, 0);
    if (!clock) return clock.status();
    QueueMetrics(indexManager_).GetMvrt(minVirtResTime, size, slots);
    return 0;
}

int ProductQueue::clearMinVirtResTimeMetrics() {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, RegionFlags::Write);
    if (!clock) return clock.status();
    QueueMetrics(indexManager_).ClearMvrt();
    return 0;
}

int ProductQueue::dumpFreeExtents() {
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, 0);
    if (!clock) return clock.status();
    RegionList* rl = indexManager_.GetRegionList();
    size_t   spix = rl->fext_;
    Region* spp = &(*rl)[spix];
    size_t   sqix = (*indexManager_.GetFreeBlock())[spp->prev];
    LogDebug("** Free list extents:\t");
    while (sqix != RL_FEXT_TL) {
        spix = sqix;
        spp = &(*rl)[spix];
        LogDebug("{} ", spp->GetExtent());
        sqix = (*indexManager_.GetFreeBlock())[spp->prev];
    }
    return 0;
}

int ProductQueue::getStats(size_t& nprods, size_t& nfree, size_t& nempty, size_t& nbytes,
                           size_t& maxprods, size_t& maxfree, size_t& minempty, size_t& maxbytes,
                           double& age_oldest, size_t& maxextent)
{
    //if (fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!file_.isOpen()){
      return static_cast<int>(PqStatus::Invalid);
    }
    ThreadLock tlock(*mutex_);
    ControlLock clock(*this, 0);
    if (!clock) return clock.status();
    QueueMetrics(indexManager_).GetStats(
        nprods, nfree, nempty, nbytes, maxprods, maxfree, minempty, maxbytes, age_oldest, maxextent
    );
    return 0;
}

int ProductQueue::getWriteCount(size_t& write_count) {
    //if (fd_ != -1) {
    if (file_.isOpen()) {
        ThreadLock tlock(*mutex_);
        ControlLock clock(*this, 0);
        if (!clock) return clock.status();
        write_count = static_cast<size_t>(indexManager_.GetControl()->write_count);
        return 0;
    }
    std::string path = getPathname();
    if (path.empty()) path = registry::getQueuePath();
    ProductQueue localPq(nullptr);
    int status = localPq.open(path, PqFlags::ReadOnly);
    if (status == 0) {
        status = localPq.getWriteCount(write_count);
    }
    return status;
}

int ProductQueue::clearWriteCount() {
    //if (fd_ != -1) {
    if (file_.isOpen()) {
        ThreadLock tlock(*mutex_);
        ControlLock clock(*this, RegionFlags::Write);
        if (!clock) return clock.status();
        indexManager_.GetControl()->write_count = 0;
        return 0;
    }
    std::string path = getPathname();
    if (path.empty()) path = registry::getQueuePath();
    ProductQueue localPq(nullptr);
    int status = localPq.open(path, 0);
    if (status == 0) {
        status = localPq.clearWriteCount();
    }
    return status;
}

std::unique_ptr<IQueueCursor> ProductQueue::CreateCursor() { 
    return std::make_unique<QueueCursor>(*this); 
}

}
