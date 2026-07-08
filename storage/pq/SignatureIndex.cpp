#include "config.h"
#include "Log.h"
#include "SignatureIndex.h"
#include "MathUtil.h"

#include <cstring>
#include <cerrno>

namespace rdm {

namespace {
    inline size_t calc_nchains(size_t const nelems) {
        return rdm::prevPrime(nelems / SX_EXP_CHAIN_LEN);
    }
}

size_t SignatureIndex::GetRequiredSize(const size_t nelems) {
    log_assert(nelems);
    static size_t prev_nelems = 0;
    static size_t cached_size = 0;
    
    if (nelems != prev_nelems) {
        prev_nelems = nelems;
        
        size_t elem_size = sizeof(SignatureIndex) - sizeof(SignatureElement) * SX_NALLOC_INITIAL;
        elem_size += nelems * sizeof(SignatureElement);
        
        size_t chains = calc_nchains(nelems);
        size_t hash_size = sizeof(SignatureHash) - sizeof(size_t) * SXHASH_NALLOC_INITIAL;
        hash_size += chains * sizeof(size_t);
        
        cached_size = elem_size + hash_size;
    }
    return cached_size;
}

SignatureHash* SignatureIndex::GetHashTable() {
    // The hash table is dynamically packed into memory immediately after the elements
    return reinterpret_cast<SignatureHash*>(&sxep[nalloc]);
}

const SignatureHash* SignatureIndex::GetHashTable() const {
    return reinterpret_cast<const SignatureHash*>(&sxep[nalloc]);
}

size_t SignatureIndex::Hash(const Signature& sig) const {
    unsigned int n = 0;
    // Just like the legacy logic, we use the first 4 bytes of the signature for the hash
    for (int i = 0; i < 4; i++) {
        n = 256 * n + sig[i];
    }
    return n % nchains;
}

void SignatureIndex::Initialize(size_t allocSize) {
    nalloc = allocSize;
    nelems = 0;
    nchains = calc_nchains(allocSize);
    
    SignatureHash* sxhp = GetHashTable();
    sxhp->magic = SX_MAGIC;
    for (size_t i = 0; i < nchains; i++) {
        sxhp->chains[i] = SX_NONE;
    }

    SignatureElement* current_elem;
    SignatureElement* const end = &sxep[nalloc];
    off_t isx = 1;
    
    for (current_elem = &sxep[0]; current_elem < end; current_elem++, isx++) {
        current_elem->sxi.fill(0);
        current_elem->offset = static_cast<off_t>(-1); // OFF_NONE
        current_elem->next = isx;
    }
    
    current_elem = &sxep[isx - 2];
    current_elem->next = SX_NONE;
    free_head = 0;
    nfree = nalloc;
}

size_t SignatureIndex::AllocateElement() {
    if (nfree == 0) return SX_NONE;
    size_t avail = free_head;
    SignatureElement* elem = &sxep[avail];
    free_head = elem->next;
    nfree--;
    return avail;
}

void SignatureIndex::FreeElement(size_t index) {
    SignatureElement* elem = &sxep[index];
    elem->offset = static_cast<off_t>(-1);
    elem->next = free_head;
    free_head = index;
    nfree++;
}

bool SignatureIndex::Find(const Signature& sig, SignatureElement** outElement) {
    *outElement = nullptr;
    const SignatureHash* sxhp = GetHashTable();
    log_assert(sxhp->magic == SX_MAGIC);
    
    size_t hash_idx = Hash(sig);
    size_t next = sxhp->chains[hash_idx];
    
    while (next != SX_NONE) {
        SignatureElement* elem = &sxep[next];
        if (sig == elem->sxi) { // Utilizing our overloaded == operator!
            *outElement = elem;
            return true;
        }
        next = elem->next;
    }
    return false;
}

SignatureElement* SignatureIndex::Add(const Signature& sig, off_t offset) {
    SignatureHash* sxhp = GetHashTable();
    log_assert(sxhp->magic == SX_MAGIC);
    log_assert(nalloc != 0);
    log_assert(nfree + nelems == nalloc);

    size_t sxix = AllocateElement();
    if (sxix == SX_NONE) {
        LogError("SignatureIndex::Add: no slots for signatures, too many products?");
        return nullptr;
    }

    SignatureElement* elem = &sxep[sxix];
    elem->sxi = sig;
    elem->offset = offset;

    size_t hash_idx = Hash(sig);
    size_t next = sxhp->chains[hash_idx];
    
    elem->next = next;
    sxhp->chains[hash_idx] = sxix;
    nelems++;
    
    return elem;
}

bool SignatureIndex::Remove(const Signature& sig) {
    SignatureHash* sxhp = GetHashTable();
    log_assert(sxhp->magic == SX_MAGIC);
    log_assert(nfree + nelems == nalloc);

    size_t hash_idx = Hash(sig);
    size_t next = sxhp->chains[hash_idx];
    
    if (next == SX_NONE) return false;

    SignatureElement* elem = &sxep[next];
    
    if (sig == elem->sxi) {
        sxhp->chains[hash_idx] = elem->next;
        FreeElement(next);
        nelems--;
        return true;
    }

    next = elem->next;
    while (next != SX_NONE) {
        SignatureElement* prev_elem = elem;
        elem = &sxep[next];
        if (sig == elem->sxi) {
            prev_elem->next = elem->next;
            FreeElement(next);
            nelems--;
            return true;
        }
        next = elem->next;
    }
    return false;
}

}
