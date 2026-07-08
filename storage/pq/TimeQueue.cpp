#include "TimeQueue.h"
#include "config.h"
#include "Log.h"
#include <cerrno>

namespace rdm {

size_t TimeQueue::GetRequiredSize(const size_t nelems) {
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t cached_size = 0;
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        size_t base_size = sizeof(TimeQueue) - sizeof(TimeQueueElement) * TQ_NALLOC_INITIAL;
        cached_size = base_size + (nelems + TQ_OVERHEAD_ELEMS) * sizeof(TimeQueueElement);
    }
    return cached_size;
}

FreeBlock* TimeQueue::GetFreeBlock() const {
    return reinterpret_cast<FreeBlock*>(
        reinterpret_cast<char*>(const_cast<TimeQueue*>(this)) + fbp_off_
    );
}

void TimeQueue::Initialize(size_t allocSize, FreeBlock* fbp) {
    log_assert(fbp != nullptr);
    log_assert(fbp->GetMagic() == FB_MAGIC);

    nalloc_ = allocSize;
    fbp_off_ = reinterpret_cast<char*>(fbp) - reinterpret_cast<char*>(this);

    // Initialize TQ_NIL (Tail boundary)
    TimeQueueElement* tqelemp = &tqep_[TQ_NIL];
    tqelemp->tv = Timestamp::ENDT.ToTimeval();
    tqelemp->offset = static_cast<off_t>(-1);
    tqelemp->fblk = fbp->Get(0);

    // Initialize TQ_HEAD (Head boundary)
    tqelemp = &tqep_[TQ_HEAD];
    tqelemp->tv = Timestamp::NONE.ToTimeval();
    tqelemp->offset = static_cast<off_t>(-1);
    
    int maxlevel = fbp->GetMaxSize() - 1;
    tqelemp->fblk = fbp->Get(maxlevel);
    
    for(int i = 0; i < fbp->GetMaxSize(); i++) {
        (*fbp)[tqelemp->fblk + i] = TQ_NIL;
    }

    level_ = 0;
    nelems_ = TQ_OVERHEAD_ELEMS;
    nfree_ = (nalloc_ + TQ_OVERHEAD_ELEMS) - TQ_OVERHEAD_ELEMS;
    free_head_ = nelems_;

    // Initialize free list
    size_t current_nelems = nelems_;
    TimeQueueElement* const end = &tqep_[nalloc_ + TQ_OVERHEAD_ELEMS];
    for (tqelemp = &tqep_[nelems_]; tqelemp < end; tqelemp++) {
        tqelemp->tv = Timestamp::NONE.ToTimeval();
        tqelemp->offset = ++current_nelems;
        tqelemp->fblk = static_cast<fblk_t>(-1);
    }
    
    tqelemp = &tqep_[nalloc_ + TQ_OVERHEAD_ELEMS - 1];
    tqelemp->offset = TQ_NONE;
}

bool TimeQueue::HasSpace() const {
    log_assert(nelems_ - TQ_OVERHEAD_ELEMS <= nalloc_);
    return (nelems_ - TQ_OVERHEAD_ELEMS < nalloc_);
}

tqep_t TimeQueue::AllocateElement() {
    if (nfree_ > 0) {
        tqep_t result = free_head_;
        TimeQueueElement* tpp = &tqep_[result];
        free_head_ = tpp->offset;
        nfree_--;
        nelems_++;
        log_assert(result > TQ_HEAD && result != TQ_NONE);
        return result;
    }
    return TQ_NONE;
}

void TimeQueue::FreeElement(int level, tqep_t p) {
    TimeQueueElement* tpp = &tqep_[p];
    FreeBlock* fbp = GetFreeBlock();
    log_assert(fbp->GetMagic() == FB_MAGIC);
    log_assert(TQ_HEAD < p && p < nalloc_ + TQ_OVERHEAD_ELEMS);
    
    tpp->tv = Timestamp::NONE.ToTimeval();
    tpp->offset = free_head_;
    fbp->Release(level + 1, tpp->fblk);
    tpp->fblk = static_cast<fblk_t>(-1);
    free_head_ = p;
    nfree_++;
    nelems_--;
}

int TimeQueue::Add(const off_t offset) {
    FreeBlock* fbp = GetFreeBlock();
    log_assert(fbp->GetMagic() == FB_MAGIC);
    log_assert(nalloc_ != 0);
    log_assert(HasSpace());

    tqep_t tpix = AllocateElement();
    log_assert(tpix != TQ_NONE);

    TimeQueueElement* tp = &tqep_[tpix];
    tp->tv = Timestamp::Now().ToTimeval();
    
    auto GetNext = [&](TimeQueueElement* elt, int k) -> TimeQueueElement* {
        return &tqep_[(*fbp)[elt->fblk + k]];
    };
    auto IndexNext = [&](TimeQueueElement* elt, int k) -> fblk_t& {
        return (*fbp)[elt->fblk + k];
    };

    TimeQueueElement* tpp = &tqep_[TQ_HEAD];
    int k = level_;
    TimeQueueElement* update[15]; // MAXLEVELS

    do {
        TimeQueueElement* tqp = GetNext(tpp, k);
        while (Timestamp::FromTimeval(tqp->tv) < Timestamp::FromTimeval(tp->tv)) {
            tpp = tqp;
            tqp = GetNext(tpp, k);
        }
        if (Timestamp::FromTimeval(tqp->tv) != Timestamp::FromTimeval(tp->tv)) {
            update[k--] = tpp;
        } else {
            Timestamp tempTs = Timestamp::FromTimeval(tp->tv);
            tempTs.IncrementMicrosecond();
            tp->tv = tempTs.ToTimeval();
            if (k < level_) {
                k = level_;
                tpp = update[k];
            }
        }
    } while (k >= 0);

    k = fbp->GetRandomLevel();
    if (k > level_) k = level_ + 1;
    
    fblk_t fblk = fbp->Get(k);
    if (fblk == static_cast<fblk_t>(-1)) {
        return ENOSPC;
    }

    tp->fblk = fblk;
    tp->offset = offset;
    
    if (k > level_) {
        for (int i = level_ + 1; i <= k; i++) update[i] = &tqep_[TQ_HEAD];
        level_ = k;
    }
    
    do {
        tpp = update[k];
        IndexNext(tp, k) = IndexNext(tpp, k);
        IndexNext(tpp, k) = tpix;
    } while(--k >= 0);

    return 0;
}

TimeQueueElement* TimeQueue::Find(const struct timeval* key, const Match mt) const {
    if (nelems_ - TQ_OVERHEAD_ELEMS == 0) return nullptr;

    FreeBlock* fbp = GetFreeBlock();
    log_assert(fbp->GetMagic() == FB_MAGIC);

    tqep_t p = TQ_HEAD;
    const TimeQueueElement* tpp = &tqep_[p];
    int k = level_;
    tqep_t q;
    const TimeQueueElement* tqp;

    Timestamp targetTs = Timestamp::FromTimeval(*key);

    do {
        q = (*fbp)[tpp->fblk + k];
        tqp = &tqep_[q];

        while (Timestamp::FromTimeval(tqp->tv) < targetTs) {
            p = q;
            tpp = &tqep_[p];
            q = (*fbp)[tpp->fblk + k];
            tqp = &tqep_[q];
        }
    } while(--k >= 0);

    Timestamp foundTs = Timestamp::FromTimeval(tqp->tv);

    switch (mt) {
        case Match::LessThan:
            if (p == TQ_HEAD) return nullptr;
            return const_cast<TimeQueueElement*>(tpp);

        case Match::Equal:
            if (foundTs == targetTs) return const_cast<TimeQueueElement*>(tqp);
            return nullptr;

        case Match::GreaterThan:
            if (q == TQ_NIL) return nullptr;
            if (foundTs == targetTs) {
                q = (*fbp)[tqp->fblk];
                tqp = &tqep_[q];
                if (q == TQ_NIL) return nullptr;
                return const_cast<TimeQueueElement*>(tqp);
            }
            return const_cast<TimeQueueElement*>(tqp);
    }

    LogError("TimeQueue::Find: bad match parameter");
    return nullptr;
}

TimeQueueElement* TimeQueue::First() const {
    FreeBlock* fbp = GetFreeBlock();
    log_assert(fbp->GetMagic() == FB_MAGIC);

    tqep_t p = TQ_HEAD;
    const TimeQueueElement* tpp = &tqep_[p];
    tqep_t q = (*fbp)[tpp->fblk];
    
    if (q == TQ_NIL) return nullptr;
    return const_cast<TimeQueueElement*>(&tqep_[q]);
}

void TimeQueue::Delete(TimeQueueElement* tqep) {
    FreeBlock* fbp = GetFreeBlock();
    log_assert(fbp->GetMagic() == FB_MAGIC);

    tqep_t p = TQ_HEAD;
    const TimeQueueElement* tpp = &tqep_[p];
    int m = level_;
    int k = m;
    tqep_t q;
    const TimeQueueElement* tqp;
    tqep_t update[15]; // MAXLEVELS

    Timestamp targetTs = Timestamp::FromTimeval(tqep->tv);

    do {
        q = (*fbp)[tpp->fblk + k];
        tqp = &tqep_[q];
        
        log_assert((q == TQ_NIL) || (TQ_HEAD < q && q < nalloc_ + TQ_OVERHEAD_ELEMS));
        
        while (Timestamp::FromTimeval(tqp->tv) < targetTs || 
              (Timestamp::FromTimeval(tqp->tv) == targetTs && tqp->offset < tqep->offset)) {
            p = q;
            tpp = tqp;
            q = (*fbp)[tpp->fblk + k];
            tqp = &tqep_[q];
            log_assert((q == TQ_NIL) || (TQ_HEAD < q && q < nalloc_ + TQ_OVERHEAD_ELEMS));
        }
        update[k] = p;
    } while(--k >= 0);

    log_assert((q == TQ_NIL) || (TQ_HEAD < q && q < nalloc_ + TQ_OVERHEAD_ELEMS));

    if (Timestamp::FromTimeval(tqp->tv) == targetTs && tqp->offset == tqep->offset) {
        for(k = 0; k <= m; k++) {
            p = update[k];
            tpp = &tqep_[p];
            if ((*fbp)[tpp->fblk + k] != q) break;
            (*fbp)[tpp->fblk + k] = (*fbp)[tqp->fblk + k];
        }
        
        FreeElement(k - 1, q);
        
        p = TQ_HEAD;
        tpp = &tqep_[p];
        while((*fbp)[tpp->fblk + m] == TQ_NIL && m > 0) m--;
        level_ = m;
    }
}

TimeQueueElement* TimeQueue::Next(const TimeQueueElement* tqep) const {
    FreeBlock* fbp = GetFreeBlock();
    log_assert(fbp->GetMagic() == FB_MAGIC);
    return const_cast<TimeQueueElement*>(&tqep_[(*fbp)[tqep->fblk]]);
}

}
