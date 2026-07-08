#include "RegionList.h"
#include "MathUtil.h"
#include "config.h"
#include "Log.h"
#include <cerrno>
#include <cstring>
#include <cstdlib>

namespace rdm {

namespace {
    inline size_t CalculateNumChains(size_t const nelems) {
        return rdm::prevPrime(nelems / RL_EXP_CHAIN_LEN);
    }
}

RegionList::RegionHash* RegionList::GetHashTable() {
    return reinterpret_cast<RegionHash*>(&rp_[nalloc_ + RL_FREE_OVERHEAD]);
}

const RegionList::RegionHash* RegionList::GetHashTable() const {
    return reinterpret_cast<const RegionHash*>(&rp_[nalloc_ + RL_FREE_OVERHEAD]);
}

FreeBlock* RegionList::GetFreeBlock() const {
    return reinterpret_cast<FreeBlock*>(
        reinterpret_cast<char*>(const_cast<RegionList*>(this)) + fbp_off_
    );
}

size_t RegionList::GetRequiredSize(const size_t nelems) {
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t cached_size = 0;
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        size_t array_size = sizeof(RegionList) - sizeof(Region) * RL_NALLOC_INITIAL;
        array_size += (nelems + RL_FREE_OVERHEAD) * sizeof(Region);
        size_t nchains = CalculateNumChains(nelems);
        size_t hash_size = sizeof(RegionHash) - sizeof(size_t);
        hash_size += nchains * sizeof(size_t);
        cached_size = array_size + hash_size;
    }
    return cached_size;
}

void RegionList::InitArray() {
    Region* const end = rp_ + (nalloc_ + RL_FREE_OVERHEAD);
    off_t irl = RL_EMPTY_HD + 1;
    Region* rep;
    for(rep = rp_ + RL_EMPTY_HD; rep < end; rep++, irl++) {
        rep->offset = static_cast<off_t>(-1);
        rep->extent = 0;
        rep->next = irl;
    }
    rep = rp_ + (irl - 2);
    rep->next = RL_NONE;
}

size_t RegionList::GetEmptyRegion() {
    if (empty_ == RL_NONE) {
        log_assert(nempty_ == 0);
        return RL_NONE;
    }
    size_t ix = empty_;
    Region* rep = rp_ + ix;
    empty_ = rep->next;
    nempty_--;
    if (nempty_ < minempty_) minempty_ = nempty_;
    return ix;
}

void RegionList::ReleaseEmptyRegion(size_t rlix) {
    Region* rep = rp_ + rlix;
    log_assert(0 < rlix && rlix < nalloc_ + RL_FREE_OVERHEAD);
    rep->next = empty_;
    rep->offset = static_cast<off_t>(-1);
    rep->extent = 0;
    empty_ = rlix;
    nempty_++;
}

size_t RegionList::HashOffset(off_t offset) const {
    unsigned int n = offset;
    return n % nchains_;
}

void RegionList::InitHash() {
    RegionHash* rlhp = GetHashTable();
    rlhp->magic = RL_MAGIC;
    for (size_t i = 0; i < nchains_; i++) {
        rlhp->chains[i] = RL_NONE;
    }
}

void RegionList::InitOffsets() {
    const off_t huge_off_t = ((off_t)1 << (sizeof(off_t)*CHAR_BIT - 2)) +
                             (((off_t)1 << (sizeof(off_t)*CHAR_BIT - 2)) - 1);
    FreeBlock* fbp = GetFreeBlock();
    Region* foff_hd = rp_ + RL_FOFF_HD;
    foff_hd->offset = 0;
    foff_hd->extent = 0;
    int maxlevel = fbp->GetMaxSize() - 1;
    foff_hd->next = fbp->Get(maxlevel);
    foff_hd->prev = 0;

    Region* foff_tl = rp_ + RL_FOFF_TL;
    foff_tl->offset = huge_off_t;
    foff_tl->extent = 0;
    foff_tl->next = fbp->Get(0);
    foff_tl->prev = 0;

    for(int i = 0; i < fbp->GetMaxSize(); i++) {
        (*fbp)[foff_hd->next + i] = RL_FOFF_TL;
    }
    level_foff_ = 0;
    foff_ = RL_FOFF_HD;
}

void RegionList::InitExtents() {
    FreeBlock* fbp = GetFreeBlock();
    Region* fext_hd = rp_ + RL_FEXT_HD;
    fext_hd->offset = 0;
    fext_hd->extent = 0;
    int maxlevel = fbp->GetMaxSize() - 1;
    fext_hd->next = 0;
    fext_hd->prev = fbp->Get(maxlevel);

    Region* fext_tl = rp_ + RL_FEXT_TL;
    fext_tl->offset = 0;
    fext_tl->extent = static_cast<size_t>(-1);
    fext_tl->ClearAlloc();
    fext_tl->next = 0;
    fext_tl->prev = fbp->Get(0);

    for(int i = 0; i < fbp->GetMaxSize(); i++) {
        (*fbp)[fext_hd->prev + i] = RL_FEXT_TL;
    }
    level_fext_ = 0;
    fext_ = RL_FEXT_HD;
}

void RegionList::Initialize(size_t nalloc, FreeBlock* fbp) {
    log_assert(fbp->GetMagic() == FB_MAGIC);
    nalloc_ = nalloc;
    nchains_ = CalculateNumChains(nalloc);
    InitHash();
    log_assert(GetHashTable()->magic == RL_MAGIC);
    InitArray();
    empty_ = RL_EMPTY_HD;
    nelems_ = 0;
    maxelems_ = nelems_;
    nempty_ = nalloc;
    minempty_ = nempty_;
    nbytes_ = 0;
    maxbytes_ = nbytes_;
    fbp_off_ = reinterpret_cast<char*>(fbp) - reinterpret_cast<char*>(this);
    nfree_ = 0;
    maxfree_ = nfree_;
    maxfextent_ = 0;
    InitOffsets();
    InitExtents();
    log_assert(nelems_ + nfree_ + nempty_ == nalloc_);
}

// ==============================================================================
// Skip-List Search Helpers
// ==============================================================================

void RegionList::SearchOffset(off_t targetOffset, size_t* update, Region** outSqp) const {
    FreeBlock* fbp = GetFreeBlock();
    size_t spix = foff_;
    Region* spp = const_cast<Region*>(rp_ + spix);
    int k = level_foff_;
    size_t sqix;
    Region* sqp;

    do {
        sqix = (*fbp)[spp->next + k];
        sqp = const_cast<Region*>(rp_ + sqix);
        while(sqp->offset < targetOffset) {
            spix = sqix;
            spp = sqp;
            sqix = (*fbp)[spp->next + k];
            sqp = const_cast<Region*>(rp_ + sqix);
        }
        if (update) update[k] = spix;
    } while(--k >= 0);

    if (outSqp) *outSqp = sqp;
}

void RegionList::SearchExtent(size_t targetExtent, off_t targetOffset, size_t* update, Region** outSqp) const {
    FreeBlock* fbp = GetFreeBlock();
    size_t spix = fext_;
    Region* spp = const_cast<Region*>(rp_ + spix);
    int k = level_fext_;
    size_t sqix;
    Region* sqp;

    do {
        sqix = (*fbp)[spp->prev + k];
        sqp = const_cast<Region*>(rp_ + sqix);
        while(sqp->GetExtent() < targetExtent ||
              (sqp->GetExtent() == targetExtent && sqp->offset < targetOffset)) {
            spix = sqix;
            spp = sqp;
            sqix = (*fbp)[spp->prev + k];
            sqp = const_cast<Region*>(rp_ + sqix);
        }
        if (update) update[k] = spix;
    } while(--k >= 0);

    if (outSqp) *outSqp = sqp;
}

// ==============================================================================
// End Skip-List Search Helpers
// ==============================================================================

size_t RegionList::FindPrevExtent(size_t rlix) {
    size_t update[15];
    SearchExtent(rp_[rlix].GetExtent(), rp_[rlix].offset, update, nullptr);
    return update[0];
}

size_t RegionList::RecalculateMaxFreeExtent() {
    size_t rmix = FindPrevExtent(RL_FEXT_TL);
    return rp_[rmix].GetExtent();
}

void RegionList::DeleteOffset(size_t rlix) {
    FreeBlock* fbp = GetFreeBlock();
    Region* rep = rp_ + rlix;
    size_t update[15];
    Region* sqp = nullptr;

    SearchOffset(rep->offset, update, &sqp);

    if (sqp && sqp->offset == rep->offset) {
        int m = level_foff_;
        int k;
        size_t sqix = sqp - rp_;
        for(k = 0; k <= m; k++) {
            size_t spix = update[k];
            Region* spp = rp_ + spix;
            if ((*fbp)[spp->next + k] != sqix) break;
            (*fbp)[spp->next + k] = (*fbp)[sqp->next + k];
        }
        fbp->Release(k, sqp->next);

        size_t spix = foff_;
        Region* spp = rp_ + spix;
        while((*fbp)[spp->next + m] == RL_FOFF_TL && m > 0) m--;
        level_foff_ = m;
    }
}

void RegionList::DeleteExtent(size_t rlix) {
    FreeBlock* fbp = GetFreeBlock();
    Region* rep = rp_ + rlix;
    size_t update[15];
    Region* sqp = nullptr;

    SearchExtent(rep->GetExtent(), rep->offset, update, &sqp);

    if (sqp && sqp->GetExtent() == rep->GetExtent() && sqp->offset == rep->offset) {
        int m = level_fext_;
        int k;
        size_t sqix = sqp - rp_;
        for(k = 0; k <= m; k++) {
            size_t spix = update[k];
            Region* spp = rp_ + spix;
            if ((*fbp)[spp->prev + k] != sqix) break;
            (*fbp)[spp->prev + k] = (*fbp)[sqp->prev + k];
        }
        fbp->Release(k, sqp->prev);

        size_t spix = fext_;
        Region* spp = rp_ + spix;
        while((*fbp)[spp->prev + m] == RL_FEXT_TL && m > 0) m--;
        level_fext_ = m;
    }
}

size_t RegionList::FindExtent(size_t extent) {
    Region* sqp = nullptr;
    // Pass -1 to ensure offset tie-breaker correctly skips to the first matching extent
    SearchExtent(extent, -1, nullptr, &sqp);
    return sqp ? (sqp - rp_) : RL_NONE;
}

size_t RegionList::Get(size_t extent) {
    if(extent > maxfextent_) return RL_NONE;

    size_t sqbest = FindExtent(extent);
    if(sqbest == RL_FEXT_TL || sqbest == RL_NONE) return RL_NONE;

    Region* rep = rp_ + sqbest;
    DeleteOffset(sqbest);
    DeleteExtent(sqbest);

    nfree_--;
    if(rep->GetExtent() == maxfextent_) {
        maxfextent_ = RecalculateMaxFreeExtent();
    }
    nelems_++;
    if (nelems_ > maxelems_) maxelems_ = nelems_;

    return sqbest;
}

void RegionList::RemoveFromHash(size_t rlix) {
    Region* rep = rp_ + rlix;
    size_t rpix = rep->prev;
    size_t rnix = rep->next;

    if(rpix != RL_NONE) {
        rp_[rpix].next = rnix;
    } else {
        size_t hash_idx = HashOffset(rep->offset);
        RegionHash* rlhp = GetHashTable();
        rlhp->chains[hash_idx] = rnix;
    }

    if(rnix != RL_NONE) {
        log_assert(rp_[rnix].IsAlloc());
        rp_[rnix].prev = rpix;
    }
}

int RegionList::AddOffset(size_t rlix) {
    FreeBlock* fbp = GetFreeBlock();
    Region* rep = rp_ + rlix;
    size_t update[15];

    SearchOffset(rep->offset, update, nullptr);

    int k = fbp->GetRandomLevel();
    if (k > level_foff_) {
        level_foff_++;
        k = level_foff_;
        update[k] = foff_;
    }

    rep->next = fbp->Get(k);
    if (rep->next == FBLK_NONE) {
        LogError("Couldn't get skip-list node of level {}", k);
        return -4;
    }

    do {
        size_t spix = update[k];
        Region* spp = rp_ + spix;
        (*fbp)[rep->next + k] = (*fbp)[spp->next + k];
        (*fbp)[spp->next + k] = rlix;
    } while(--k >= 0);

    return 0;
}

int RegionList::AddExtent(size_t rlix) {
    FreeBlock* fbp = GetFreeBlock();
    Region* rep = rp_ + rlix;
    size_t update[15];

    SearchExtent(rep->GetExtent(), rep->offset, update, nullptr);

    int k = fbp->GetRandomLevel();
    if (k > level_fext_) {
        level_fext_++;
        k = level_fext_;
        update[k] = fext_;
    }

    rep->prev = fbp->Get(k);
    if (rep->prev == FBLK_NONE) {
        LogError("Couldn't get new skip-list node of level {}", k);
        return -4;
    }

    do {
        size_t spix = update[k];
        Region* spp = rp_ + spix;
        (*fbp)[rep->prev + k] = (*fbp)[spp->prev + k];
        (*fbp)[spp->prev + k] = rlix;
    } while(--k >= 0);

    return 0;
}

int RegionList::ReleaseRegion(size_t rlix) {
    int status = AddOffset(rlix);
    if (status) {
        LogError("Couldn't add to offset free-list");
    } else {
        status = AddExtent(rlix);
        if (status) {
            LogError("Couldn't add to extent free-list");
            DeleteOffset(rlix);
        } else {
            nfree_++;
        }
    }
    return status;
}

size_t RegionList::GetNextOffset(size_t rlix) {
    FreeBlock* fbp = GetFreeBlock();
    Region* rep = rp_ + rlix;
    log_assert(rep->IsFree());
    return (*fbp)[rep->next];
}

size_t RegionList::GetPrevOffset(size_t rlix) {
    size_t update[15];
    SearchOffset(rp_[rlix].offset, update, nullptr);
    return update[0];
}

void RegionList::Consolidate(size_t rpix) {
    Region* rep = rp_ + rpix;
    int nmerges = 0;
    size_t rghtix = GetNextOffset(rpix);
    size_t leftix = GetPrevOffset(rpix);

    if(rghtix != RL_FOFF_TL) {
        Region* rght = rp_ + rghtix;
        if(rep->offset + static_cast<off_t>(rep->GetExtent()) == rght->offset) {
            DeleteExtent(rpix);
            rep->extent += rght->GetExtent();
            AddExtent(rpix);
            nfree_--;
            DeleteOffset(rghtix);
            DeleteExtent(rghtix);
            ReleaseEmptyRegion(rghtix);
            nmerges++;
        }
    }

    if(leftix != RL_FOFF_HD) {
        Region* left = rp_ + leftix;
        if(left->offset + static_cast<off_t>(left->GetExtent()) == rep->offset) {
            DeleteExtent(leftix);
            left->extent += rep->GetExtent();
            AddExtent(leftix);
            nfree_--;
            DeleteOffset(rpix);
            DeleteExtent(rpix);
            ReleaseEmptyRegion(rpix);
            nmerges++;
            rep = left;
        }
    }

    if(rep->GetExtent() > maxfextent_) {
        maxfextent_ = rep->GetExtent();
    }
}

size_t RegionList::Find(off_t offset) const {
    const RegionHash* rlhp = GetHashTable();
    log_assert(rlhp->magic == RL_MAGIC);
    size_t hash_idx = HashOffset(offset);
    size_t next = rlhp->chains[hash_idx];

    while (next != RL_NONE) {
        const Region* rep = rp_ + next;
        if(offset == rep->offset) {
            log_assert(rep->IsAlloc());
            return next;
        }
        next = rep->next;
    }
    return RL_NONE;
}

bool RegionList::Find(off_t offset, Region** rpp) {
    size_t rlix = Find(offset);
    if(rlix == RL_NONE) {
        *rpp = nullptr;
        return false;
    }
    *rpp = rp_ + rlix;
    return true;
}

void RegionList::AddToHash(size_t rpix) {
    Region* rep = rp_ + rpix;
    RegionHash* rlhp = GetHashTable();
    log_assert(rlhp->magic == RL_MAGIC);
    log_assert(rep->IsAlloc());

    size_t hash_idx = HashOffset(rep->offset);
    size_t next = rlhp->chains[hash_idx];

    if (next != RL_NONE) {
        log_assert(rp_[next].IsAlloc());
        rp_[next].prev = rpix;
    }
    rep->next = next;
    rep->prev = RL_NONE;
    rlhp->chains[hash_idx] = rpix;
}

Region* RegionList::Add(off_t offset, size_t extent) {
    size_t rpix = GetEmptyRegion();
    if (rpix == RL_NONE) {
        LogError("Need more product slots, allocate more when creating queue");
        return nullptr;
    }
    log_assert(nelems_ < nalloc_);

    Region* rep = rp_ + rpix;
    rep->offset = offset;
    rep->extent = extent;

    if (ReleaseRegion(rpix) != 0) {
        LogError("Couldn't insert region into free region list");
        ReleaseEmptyRegion(rpix);
        return nullptr;
    }

    if(nfree_ > maxfree_) maxfree_ = nfree_;
    log_assert(nelems_ + nfree_ + nempty_ == nalloc_);

    return rep;
}

int RegionList::Split(size_t rlix, size_t extent) {
    Region* low = rp_ + rlix;
    log_assert(low != nullptr);
    log_assert(low->IsFree());
    log_assert(extent <= low->GetExtent());

    size_t rem = low->GetExtent() - extent;
    off_t newoff = low->offset + static_cast<off_t>(extent);

    Region* newregion = Add(newoff, rem);
    if(newregion) {
        log_assert(newregion->IsFree());
        low->extent = extent;
        if(rem > maxfextent_) maxfextent_ = rem;
        return 0;
    }

    LogError("Couldn't add split-off region to free region list");
    return ENOMEM;
}

void RegionList::Put(size_t rlix) {
    log_assert(rlix < nalloc_);
    nelems_--;
    ReleaseRegion(rlix);
    Consolidate(rlix);
    if(nfree_ > maxfree_) maxfree_ = nfree_;
    log_assert(nelems_ + nfree_ + nempty_ == nalloc_);
}

void RegionList::Free(size_t rpix) {
    Region* rep = rp_ + rpix;
    rep->ClearAlloc();
    nbytes_ -= rep->GetExtent();
    RemoveFromHash(rpix);
    nelems_--;
    ReleaseRegion(rpix);
    Consolidate(rpix);
    if(nfree_ > maxfree_) maxfree_ = nfree_;
    log_assert(nelems_ + nfree_ + nempty_ == nalloc_);
}

bool RegionList::HasSpace() const {
    return (nempty_ > 0);
}

}
