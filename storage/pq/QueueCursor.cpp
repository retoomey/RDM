#include "QueueCursor.h"
#include "ProductQueue.h"
#include "Log.h"
#include "FileUtil.h"
#include "TimeQueue.h"
#include "RegionList.h"
#include "SignatureIndex.h"
#include "MappedRegion.h"
#include <cstring>
#include <cerrno>

#define OFF_NONE ((off_t)(-1))

namespace rdm {

QueueCursor::QueueCursor(ProductQueue& pq) : pq_(pq) {
    cursor_ = Timestamp::NONE.ToTimeval();
    cursor_offset_ = OFF_NONE;
}

void QueueCursor::pq_cset(const struct timeval *tvp) {
    ThreadLock tlock(*pq_.mutex_);
    log_assert(tvp->tv_sec >= Timestamp::ZERO.tv_sec && tvp->tv_usec >= Timestamp::ZERO.tv_usec);
    cursor_ = *tvp;
    if(Timestamp::FromTimeval(*tvp) == Timestamp::ENDT) {
        cursor_offset_ = OFF_NONE;
    } else if (Timestamp::FromTimeval(*tvp) == Timestamp::ZERO) {
        cursor_offset_ = 0;
    }
}

void QueueCursor::pq_coffset(off_t c_offset) {
    ThreadLock tlock(*pq_.mutex_);
    cursor_offset_ = c_offset;
}

void QueueCursor::setCursor(const Timestamp& timestamp) {
    //if (pq_.fd_ == -1) return;
    if (!pq_.isOpen()) return;
    ThreadLock tlock(*pq_.mutex_);
    cursor_ = timestamp.ToTimeval();
    if (timestamp == Timestamp::ENDT) cursor_offset_ = OFF_NONE;
    else if (timestamp == Timestamp::ZERO) cursor_offset_ = 0;
}

void QueueCursor::setCursorOffset(const off_t offset) {
    //if (pq_.fd_ == -1) return;
    if (!pq_.isOpen()) return;
    ThreadLock tlock(*pq_.mutex_);
    cursor_offset_ = offset;
}

void QueueCursor::getCursorTimestamp(Timestamp& tv) const {
    //if (pq_.fd_ == -1) return;
    if (!pq_.isOpen()) return;
    ThreadLock tlock(*pq_.mutex_);
    tv.tv_sec = cursor_.tv_sec;
    tv.tv_usec = cursor_.tv_usec;
}

int QueueCursor::setCursorClass(Match* mtp, const ProdClass& clss) {
    //if (pq_.fd_ == -1) return EINVAL;
    if (!pq_.isOpen()) return EINVAL;
    if (clss.specs.empty() ||
      (clss.from_sec == Timestamp::NONE.tv_sec && clss.from_usec == Timestamp::NONE.tv_usec) ||
      (clss.to_sec == Timestamp::NONE.tv_sec && clss.to_usec == Timestamp::NONE.tv_usec)) {
        return EINVAL;
    }
    struct timeval from_ts = { clss.from_sec, static_cast<int32_t>(clss.from_usec) };
    struct timeval to_ts = { clss.to_sec, static_cast<int32_t>(clss.to_usec) };
    pq_cset(&from_ts);
    
    Match otherway = Match::LessThan;
    if (Timestamp::FromTimeval(from_ts) > Timestamp::FromTimeval(to_ts)) {
        if (Timestamp::FromTimeval(from_ts) == Timestamp::ENDT) {
            if (mtp != nullptr) *mtp = Match::LessThan;
            return 0;
        }
        otherway = Match::GreaterThan;
    } else {
        if (Timestamp::FromTimeval(from_ts) == Timestamp::ZERO) {
            if (mtp != nullptr) *mtp = Match::GreaterThan;
            return 0;
        }
    }
    
    ThreadLock tlock(*pq_.mutex_);
    ControlLock clock(pq_, 0);
    if (!clock) return clock.status();
    
    TimeQueueElement* tqep = pq_.indexManager_.GetTimeQueue()->Find(&from_ts, otherway);
    if (tqep != nullptr) {
        cursor_ = tqep->tv;
        cursor_offset_ = tqep->offset;
    }
    if (mtp != nullptr) {
        *mtp = (otherway == Match::LessThan) ? Match::GreaterThan : Match::LessThan;
    }
    return 0;
}

int QueueCursor::checkCursorTime(Match mt, const ProdClass& clss, const Timestamp& maxLatency) const {
    //if (pq_.fd_ == -1) return 0;
    if (!pq_.isOpen()) return 0;
    Timestamp currentCursor;
    {
        ThreadLock tlock(*pq_.mutex_);
        currentCursor = Timestamp::FromTimeval(cursor_);
    }
    if (currentCursor == Timestamp::NONE) return 0;
    if (clss.specs.empty()) return 1;
    Timestamp clssTo(clss.to_sec, clss.to_usec);
    if (mt == Match::LessThan) {
        if (currentCursor < clssTo) return 0;
    } else {
        if (currentCursor > (clssTo + maxLatency)) return 0;
    }
    return 1;
}

int QueueCursor::setCursorFromSignature(const Signature& signature) {
    //if (pq_.fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!pq_.isOpen()) return static_cast<int>(PqStatus::Invalid);
    ThreadLock tlock(*pq_.mutex_);
    ControlLock clock(pq_, 0);
    if (!clock) {
        LogSyserr("Couldn't lock control-region of product-queue");
        return clock.status();
    }
    auto decodeFunc = [&](off_t readOffset, size_t readExtent, ProdInfo& outInfo) -> int {
        auto region = pq_.regionManager_->getRegion(readOffset, readExtent, 0);
        if (!region) return EACCES;
        if (!pq_.serializer_->DecodeProdInfo(region->get(), readExtent, outInfo)) return static_cast<int>(PqStatus::Corrupt);
        return 0;
    };
    TimeQueueElement* timeEntry;
    int status = pq_.indexManager_.FindTimeEntryBySignature(signature, &timeEntry, decodeFunc);
    if (status == 0) {
        cursor_ = timeEntry->tv;
        cursor_offset_ = timeEntry->offset;
    }
    return status;
}

int QueueCursor::setCursorToLast(const ProdClass& prodClass, Timestamp& outTimestamp) {
    //if (pq_.fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!pq_.isOpen()) return static_cast<int>(PqStatus::Invalid);
    int status = 0;
    struct timeval to_ts = { prodClass.to_sec, static_cast<int32_t>(prodClass.to_usec) };
    pq_cset(&to_ts);
    
    auto matchFunc = [](const ProdInfo& info, const void* datap, void* xprod, size_t size, void* vp) -> int {
        Timestamp* tsp = static_cast<Timestamp*>(vp);
        if(tsp != nullptr) *tsp = info.arrival;
        return static_cast<int>(PqStatus::End);
    };

    //struct timeval legacy_out;
    Timestamp legacy_out;
    while ((status = sequence(Match::LessThan, prodClass, matchFunc, &legacy_out)) == 0) {
        ThreadLock tlock(*pq_.mutex_);
        if (cursor_.tv_sec < legacy_out.tv_sec) {
            outTimestamp.tv_sec = legacy_out.tv_sec;
            outTimestamp.tv_usec = legacy_out.tv_usec;
            return status;
        }
    }
    if (status != static_cast<int>(PqStatus::End)) {
        LogError("seq:{} (errno = {})", std::strerror(status), status);
    } else {
        status = 0;
    }
    
    ThreadLock tlock(*pq_.mutex_);
    if (Timestamp::FromTimeval(cursor_) == Timestamp::ENDT) {
        cursor_ = Timestamp::NONE.ToTimeval();
        cursor_offset_ = OFF_NONE;
    }
    return status;
}

int QueueCursor::sequenceHelper(Match mt, const ProdClass& clss, pq_seqfunc* ifMatch, void* otherargs, off_t* const offset) {
    int status = 0;
    //if (pq_.fd_ == -1) return static_cast<int>(PqStatus::Invalid);
    if (!pq_.isOpen()) return static_cast<int>(PqStatus::Invalid);
    off_t rp_offset = OFF_NONE;
    size_t extent = 0;
    std::unique_ptr<MappedRegion> region;
    {
        ThreadLock tlock(*pq_.mutex_);
        if (Timestamp::FromTimeval(cursor_) == Timestamp::NONE) {
            log_assert(mt != Match::Equal);
            if (mt == Match::LessThan) cursor_ = Timestamp::ENDT.ToTimeval();
            else cursor_ = Timestamp::ZERO.ToTimeval();
        }
        ControlLock clock(pq_, 0);
        if (!clock) return static_cast<int>(PqStatus::System);
        
        TimeQueueElement* tqep = pq_.indexManager_.GetTimeQueue()->Find(&cursor_, mt);
        if (tqep == nullptr) return static_cast<int>(PqStatus::End);
        
        pq_cset(&tqep->tv);
        pq_coffset(tqep->offset);
        
        if (ifMatch != nullptr) {
            Region* rp = nullptr;
            if (pq_.indexManager_.GetRegionList()->Find(tqep->offset, &rp)) {
                region = pq_.regionManager_->getRegion(rp->offset, rp->GetExtent(), 0);
                if (!region) return static_cast<int>(PqStatus::System);
                pq_.locked_count_++;
                extent = rp->GetExtent();
                rp_offset = rp->offset;
            }
        }
    }
    
    if (ifMatch != nullptr && region) {
        bool matched = false;
        ProdInfo cleanInfo;
        void* datap = nullptr;
        if (!pq_.serializer_->DecodeProdInfo(region->get(), extent, cleanInfo, &datap)) {
            status = static_cast<int>(PqStatus::System);
        } else {
            if (clss.Contains(cleanInfo)) {
                matched = true;
                if (offset) *offset = rp_offset;
                void* payloadDataPtr = static_cast<char*>(region->get()) + pq_.serializer_->GetEncodedInfoSize(cleanInfo);
                status = ifMatch(cleanInfo, payloadDataPtr, region->get(), extent, otherargs);
                if (status) {
                    ThreadLock tlock(*pq_.mutex_);
                    if (mt == Match::GreaterThan) {
                        Timestamp cursTs = Timestamp::FromTimeval(cursor_);
                        cursTs.DecrementMicrosecond();
                        cursor_ = cursTs.ToTimeval();
                        cursor_offset_ = OFF_NONE;
                    } else if (mt == Match::LessThan) {
                        cursor_offset_ = rp_offset + 1;
                    }
                }
            }
        }
        if (offset == nullptr || status || !matched) {
            pq_.locked_count_--;
        } else {
            ThreadLock tlock(*pq_.mutex_);
            pq_.activeUserRegions_[rp_offset] = std::move(region);
        }
    }
    return status;
}

int QueueCursor::next(const bool reverse, const ProdClass& prodClass, pq_next_func* const callback, const bool keepLocked, void* const appArgs) {
    int status = 0;
    //if ((pq_.fd_ == -1) || callback == nullptr) {
    if ((!pq_.isOpen()) || callback == nullptr) {
        LogError("Invalid argument");
        return static_cast<int>(PqStatus::Invalid);
    }
    
    queue_par_t queue_par;
    prod_par_t prod_par;
    std::unique_ptr<MappedRegion> region;
    off_t rp_offset;
    {
        ThreadLock tlock(*pq_.mutex_);
        if (Timestamp::FromTimeval(cursor_) == Timestamp::NONE) {
            cursor_ = (reverse ? Timestamp::ENDT : Timestamp::ZERO).ToTimeval();
        }
        ControlLock clock(pq_, 0);
        if (!clock) return static_cast<int>(PqStatus::System);
        
        queue_par.is_full = pq_.indexManager_.GetControl()->isFull;
        TimeQueueElement* tqep = pq_.indexManager_.GetTimeQueue()->Find(&cursor_,
                reverse ? Match::LessThan : Match::GreaterThan);
        if (tqep == nullptr) return static_cast<int>(PqStatus::End);
        
        TimeQueueElement* firstElem = pq_.indexManager_.GetTimeQueue()->First();
        struct timeval oldest = firstElem ? firstElem->tv : Timestamp::NONE.ToTimeval();
        queue_par.early_cursor = (Timestamp::FromTimeval(cursor_) <= Timestamp::FromTimeval(oldest));
        pq_cset(&tqep->tv);
        pq_coffset(tqep->offset);
        queue_par.inserted = Timestamp::FromTimeval(tqep->tv);
        
        Region* rp = nullptr;
        bool found = pq_.indexManager_.GetRegionList()->Find(tqep->offset, &rp);
        if (!found || rp->offset != tqep->offset || rp->GetExtent() > pq_.getDataSize()) {
            LogError("Queue corrupt: invalid region");
            return 0;
        }
        prod_par.size = rp->GetExtent();
        rp_offset = rp->offset;
        region = pq_.regionManager_->getRegion(rp_offset, prod_par.size, 0);
        if (!region) {
            LogError("Couldn't map product region at offset {}", rp_offset);
            return static_cast<int>(PqStatus::System);
        }
    }
    
    prod_par.encoded = region->get();
    ProdInfo cleanInfo;
    void* next_ptr = nullptr;
    if (!pq_.serializer_->DecodeProdInfo(prod_par.encoded, prod_par.size, cleanInfo, &next_ptr)) {
        LogError("DecodeProdInfo() failed");
        return static_cast<int>(PqStatus::System);
    }
    
    prod_par.info = cleanInfo;
    if (prodClass.Contains(cleanInfo)) {
        size_t metaSz = static_cast<char*>(next_ptr) - static_cast<char*>(prod_par.encoded);
        const size_t xsz = roundUp(prod_par.info.sz, 4);
        if (metaSz + prod_par.info.sz > xsz) {
            prod_par.size -= ((metaSz + prod_par.info.sz) - xsz);
        }
        prod_par.data = next_ptr;
        queue_par.offset = rp_offset;
        callback(&prod_par, &queue_par, appArgs);
    }
    
    if (keepLocked) {
        ThreadLock tlock(*pq_.mutex_);
        pq_.activeUserRegions_[rp_offset] = std::move(region);
    }
    return 0;
}

int QueueCursor::sequence(Match mt, const ProdClass& clss, pq_seqfunc* ifMatch, void* otherargs) {
    //if (pq_.fd_ == -1) {
    if (!pq_.isOpen()) {
      return static_cast<int>(PqStatus::Invalid);
    }
    return sequenceHelper(mt, clss, ifMatch, otherargs, nullptr);
}

int QueueCursor::sequenceDelete(Match mt, const ProdClass& clss, const int wait, size_t& extentp, Timestamp& timestampp) {
    //if (pq_.fd_ == -1) return EINVAL;
    if (!pq_.isOpen()) return EINVAL;
    ThreadLock tlock(*pq_.mutex_);
    int rflags = wait ? RegionFlags::Write : (RegionFlags::Write | RegionFlags::NoWait);
    
    if (Timestamp::FromTimeval(cursor_) == Timestamp::NONE) {
        if (mt == Match::LessThan) {
            cursor_ = Timestamp::ENDT.ToTimeval();
            cursor_offset_ = OFF_NONE;
        } else {
            cursor_ = Timestamp::ZERO.ToTimeval();
            cursor_offset_ = 0;
        }
    }
    
    ControlLock clock(pq_, rflags);
    if (!clock) return clock.status();
    
    TimeQueueElement* tqep = pq_.indexManager_.GetTimeQueue()->Find(&cursor_, mt);
    if (tqep == nullptr) return static_cast<int>(PqStatus::End);
    
    size_t rlix = pq_.indexManager_.GetRegionList()->Find(tqep->offset);
    log_assert(rlix != rdm::RL_NONE);
    const Region* rp = &(*pq_.indexManager_.GetRegionList())[rlix];
    auto region = pq_.regionManager_->getRegion(rp->offset, rp->GetExtent(), rflags);
    if (!region) return EACCES;
    
    pq_cset(&tqep->tv);
    pq_coffset(OFF_NONE);
    extentp = rp->GetExtent();
    ProdInfo cleanInfo;
    
    if (!pq_.serializer_->DecodeProdInfo(region->get(), extentp, cleanInfo)) {
        return EIO;
    }
    timestampp = cleanInfo.arrival;
    if (clss.Contains(cleanInfo)) {
        pq_.indexManager_.GetTimeQueue()->Delete(tqep);
        pq_.indexManager_.GetSignatureIndex()->Remove(cleanInfo.signature);
        pq_.indexManager_.GetRegionList()->Free(rlix);
    }
    return 0;
}

}
