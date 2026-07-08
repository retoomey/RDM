#include "config.h"
#include "IndexLayer.h"
#include "ProductQueue.h"
#include "FileUtil.h"
#include <cstdint>
#include "Log.h"

using namespace rdm;

namespace rdm {

size_t ix_sz(const size_t nelems, const size_t align) {
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t size = 0;

    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        size = roundUp(RegionList::GetRequiredSize(nelems), align) +
               roundUp(TimeQueue::GetRequiredSize(nelems), align) +
               roundUp(FreeBlock::GetRequiredSize(nelems), align) +
               roundUp(SignatureIndex::GetRequiredSize(nelems), align);
    }
    return size;
}

int ix_ptrs(
        void* const ix,
        const size_t ixsz,
        const size_t nelems,
        const size_t align,
        RegionList** const rlpp,
        TimeQueue** const tqpp,
        FreeBlock** const fbpp,
        SignatureIndex** const sxpp)
{
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t rl_size;
    static size_t tq_size;
    static size_t fb_size;
    static size_t sx_size;

    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        rl_size = RegionList::GetRequiredSize(nelems);
        tq_size = TimeQueue::GetRequiredSize(nelems);
        fb_size = FreeBlock::GetRequiredSize(nelems);
        sx_size = SignatureIndex::GetRequiredSize(nelems);
    }

    *rlpp = static_cast<RegionList*>(ix);
    
    *tqpp = reinterpret_cast<TimeQueue*>(
        roundUp(reinterpret_cast<uintptr_t>(reinterpret_cast<char*>(*rlpp) + rl_size), align));
        
    *fbpp = reinterpret_cast<FreeBlock*>(
        roundUp(reinterpret_cast<uintptr_t>(reinterpret_cast<char*>(*tqpp) + tq_size), align));
        
    *sxpp = reinterpret_cast<SignatureIndex*>(
        roundUp(reinterpret_cast<uintptr_t>(reinterpret_cast<char*>(*fbpp) + fb_size), align));

    bool bounds_check = (reinterpret_cast<char*>(*sxpp) + sx_size) <= (static_cast<char*>(ix) + ixsz);

#ifdef NDEBUG
    if (!bounds_check) {
      LogError("ix={}, ixsz={}, nelems={}, align={}, rl_size={}, "
               "tq_size={}, fb_size={}, sx_size={}, *sxpp={}",
               static_cast<const void*>(ix), ixsz, nelems, align, rl_size,
               tq_size, fb_size, sx_size, static_cast<const void*>(*sxpp));
      return 0;
    }
#else
    log_assert(bounds_check);
#endif

    return 1;
}
}
