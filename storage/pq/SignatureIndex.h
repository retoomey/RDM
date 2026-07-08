#pragma once

#include "Signature.h"
#include <sys/types.h>
#include <cstddef>
#include <type_traits>

namespace rdm {
constexpr size_t SX_NONE = static_cast<size_t>(-1);
constexpr size_t SX_EXP_CHAIN_LEN  = 4;
constexpr size_t SX_NALLOC_INITIAL = 9;
constexpr size_t SX_MAGIC = 0x53584841;
constexpr size_t SXHASH_NALLOC_INITIAL = 2;

struct SignatureElement {
  Signature sxi;
  off_t     offset;
  size_t    next;
};

struct SignatureHash {
  size_t magic;
  size_t chains[SXHASH_NALLOC_INITIAL];
};

struct SignatureIndex {
private:
  size_t           nalloc;
  size_t           nelems;
  size_t           nchains;
  size_t           free_head;
  size_t           nfree;
  SignatureElement sxep[SX_NALLOC_INITIAL];

  SignatureHash *
  GetHashTable();
  const SignatureHash *
  GetHashTable() const;
  size_t
  Hash(const Signature& sig) const;
  size_t
  AllocateElement();
  void
  FreeElement(size_t index);

public:
  size_t
  GetAllocSize() const { return nalloc; }

  static size_t
  GetRequiredSize(size_t nelems);

  void
  Initialize(size_t allocSize);
  bool
  Find(const Signature& sig, SignatureElement ** outElement);
  SignatureElement *
  Add(const Signature& sig, off_t offset);
  bool
  Remove(const Signature& sig);
};

// CRITICAL: Memory layout MUST remain intact for the memory-mapped file
static_assert(std::is_standard_layout_v<SignatureIndex>,
  "SignatureIndex must be standard-layout for mmap compatibility");
}
