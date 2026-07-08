#include "FreeBlock.h"
#include "MathUtil.h"
#include "config.h"
#include "Log.h"

#include <cmath>
#include <cstdlib>

namespace rdm {

int FreeBlock::GetRandomLevel() const {
    int level;
    for (level = 0; level < maxsize_ - 1; level++) {
        constexpr int BITS_IN_RANDOM = 31;
        constexpr int BITS_IN_PIECE = 2;
        constexpr long PIECE_MASK = (1L << BITS_IN_PIECE) - 1;
        
        static int randomsLeft = 0;
        static long randomBits = 0;
        static unsigned short xsubi[3] = {
            static_cast<unsigned short>(0x473da8f190d5f1c4u),
            static_cast<unsigned short>(0x440937acf01c8c4eu),
            static_cast<unsigned short>(0xa8a9d686bec2da48u)
        };

        if (--randomsLeft <= 0) {
            randomBits = nrand48(xsubi);
            randomsLeft = BITS_IN_RANDOM / BITS_IN_PIECE;
        }
        if (randomBits & PIECE_MASK) break;
        randomBits >>= BITS_IN_PIECE;
    }
    return level;
}

size_t FreeBlock::GetArenaSize(size_t nelems) {
    int maxsize = log4(nelems) + 1;
    int numblks = static_cast<int>(0.75 * nelems);
    size_t total = 0;

    for (int level = 0; level < maxsize; level++) {
        int blksize = level + 1;
        total += static_cast<size_t>(blksize) * numblks;
        if (numblks >= 4) {
            numblks /= 4;
        } else {
            numblks = 1;
        }
    }
    total += 3 * std::sqrt(static_cast<double>(nelems)) * log4(nelems) * maxsize;
    return total;
}

size_t FreeBlock::GetRequiredSize(const size_t nelems) {
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t cached_size = 0;

    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        cached_size = sizeof(FreeBlock) - sizeof(fblk_t) * FBLKS_NALLOC_INITIAL;
        cached_size += GetArenaSize(nelems) * sizeof(fblk_t);
    }
    return cached_size;
}

void FreeBlock::DumpStats() const {
    LogError("maxsize = {}", maxsize_);
    LogError("arena_sz = {}", arena_sz_);
    LogError("avail = {}", avail_);
    LogError("allocated = {}", allocated_);
    for (int level = 0; level <= maxsize_; level++) {
        LogError("nfree[{}]:\t{}\t{}", level, nfree_[level], free_[level]);
    }
}

void FreeBlock::InitLevel(fblk_t* offset, int level, int blksize, int numblks) {
    log_assert(level >= 0 && level <= maxsize_);
    log_assert(blksize > 0 && blksize >= level);
    log_assert(numblks > 0);

    fblk_t off = *offset;
    free_[level] = off;

    for (int i = 0; i < numblks - 1; i++) {
        fblks_[off] = off + blksize;
        off += blksize;
    }
    
    fblks_[off] = FBLK_NONE;
    nfree_[level] = numblks;
    avail_ += numblks;
    
    *offset = (off + blksize);
}

void FreeBlock::Initialize(size_t nalloc) {
    magic_ = FB_MAGIC;
    log_assert(nalloc > 0);
    
    const int maxsize = log4(nalloc) + 1;
    log_assert(maxsize < FB_MAX_LEVELS);
    maxsize_ = maxsize;
    
    size_t fblk_sz = GetArenaSize(nalloc);
    for(size_t i = 0; i < fblk_sz; i++) {
        fblks_[i] = FBLK_NONE;
    }
    
    allocated_ = 0;
    avail_ = 0;
    fblk_t offset = 0;
    int numblks = static_cast<int>(0.75 * nalloc);
    
    for (int level = 0; level < maxsize; level++) {
        InitLevel(&offset, level, level + 1, numblks);
        if (numblks >= 4) {
            numblks /= 4;
        } else {
            numblks = 1;
        }
    }
    
    numblks = static_cast<int>(3 * std::sqrt(static_cast<double>(nalloc)) * log4(nalloc));
    InitLevel(&offset, maxsize, maxsize, numblks);
    
    log_assert(fblk_sz >= offset && fblk_sz < offset + maxsize);
    arena_sz_ = offset;
}

void FreeBlock::Release(int size, fblk_t fblk) {
    int level = size - 1;
    log_assert(0 < size && size <= maxsize_);
    log_assert(fblk < arena_sz_);

    fblks_[fblk] = free_[level];
    free_[level] = fblk;
    nfree_[level]++;
    avail_++;
    allocated_--;
}

fblk_t FreeBlock::Get(int level) {
    log_assert(0 <= level && level < maxsize_);
    
    for (const int wantSize = level + 1; level <= maxsize_; level++) {
        if (nfree_[level] > 0) {
            fblk_t fblk = free_[level];
            log_assert(fblk != FBLK_NONE);
            log_assert(fblk < arena_sz_);
            
            free_[level] = fblks_[fblk];
            log_assert(nfree_[level] > 0);
            nfree_[level]--;
            
            log_assert(avail_ > 0);
            avail_--;
            allocated_++;
            
            const int gotSize = level + 1;
            if (level < maxsize_ && wantSize < gotSize) {
                fblk_t fblk2 = fblk + wantSize;
                Release(gotSize - wantSize, fblk2);
                allocated_++;
            }
            return fblk;
        }
    }
    
    LogError("\"fblk\" subsystem ran out of skip-list nodes. Too many products in queue.");
    DumpStats();
    return FBLK_NONE;
}

}
