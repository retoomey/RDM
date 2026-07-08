#pragma once
#include "Product.h"
#include "ProdClass.h"
#include "Signature.h"
#include "Timestamp.h"

#include <string>
#include <memory>

#include <sys/types.h>

namespace rdm {
struct pqe_index {
  off_t          offset;
  rdm::Signature signature;
  bool           sig_is_set;
};

struct prod_par_t {
  rdm::ProdInfo info;
  void *        data;
  void *        encoded;
  size_t        size;
};

struct queue_par_t {
  rdm::Timestamp inserted;
  off_t          offset;
  bool           early_cursor;
  bool           is_full;
  bool           is_locked;
};

typedef int pq_seqfunc(const rdm::ProdInfo& info, const void * datap, void * xprod, size_t len, void * otherargs);
typedef void pq_next_func(const prod_par_t * prod_par, const queue_par_t * queue_par, void * app_par);

class IQueueEntry {
public:
  virtual ~IQueueEntry() = default;
  virtual int commit() = 0;
  virtual int rollback() = 0;
  virtual void* getWritePointer() const = 0;
  virtual void* getPayloadPointer() const = 0;
  virtual size_t getSize() const = 0;
  virtual bool isValid() const = 0;
};

enum class PqStatus : int {
  Success  = 0,
  End      = -1,
  Dup      = -2,
  Big      = -3,
  System   = -4,
  Locked   = -5,
  Corrupt  = -6,
  NotFound = -7,
  Invalid  = -8
};

enum class Match : int {
  LessThan    = -1,
  Equal       = 0,
  GreaterThan = 1
};

struct PqFlags {
  static constexpr int Default     = 0x00;
  static constexpr int NoClobber   = 0x01;
  static constexpr int ReadOnly    = 0x02;
  static constexpr int NoLock      = 0x04;
  static constexpr int Private     = 0x08;
  static constexpr int NoGrow      = 0x10;
  static constexpr int NoMap       = 0x20;
  static constexpr int MapRgns     = 0x40;
  static constexpr int Sparse      = 0x80;
  static constexpr int ThreadSafe  = 0x100;
  static constexpr int SigsBlocked = 0x1000;
};

// IQueueCursor
// Isolates traversal and iteration state from the underlying storage mechanism.
// ==============================================================================
class IQueueCursor {
public:
  virtual
  ~IQueueCursor() = default;
  virtual void
  setCursor(const Timestamp& timestamp) = 0;
  virtual void
  setCursorOffset(const off_t offset) = 0;
  virtual int
  setCursorToLast(const ProdClass& prodClass, Timestamp& outTimestamp) = 0;
  virtual void
  getCursorTimestamp(Timestamp& tv) const = 0;
  virtual int
  setCursorClass(Match * mtp, const ProdClass& clssp) = 0;
  virtual int
  setCursorFromSignature(const Signature& signature) = 0;
  virtual int
  checkCursorTime(Match mt, const ProdClass& clss, const Timestamp& maxLatency) const = 0;
  virtual int
  next(const bool reverse, const ProdClass& prodClass, pq_next_func * const callback, const bool keepLocked,
    void * const appArgs) = 0;
  virtual int
  sequence(Match mt, const ProdClass& info, pq_seqfunc * ifMatch, void * otherargs) = 0;
  virtual int
  sequenceDelete(Match mt, const ProdClass& clss, int wait, size_t& extentp,
    Timestamp& timestampp) = 0;
};

class IProductStore {
public:
  virtual
  ~IProductStore() = default;

  // Factory method for creating independent cursors
  virtual std::unique_ptr<IQueueCursor>
  CreateCursor() = 0;

  // Storage Management
  virtual int
  open(const std::string& path, const int flags) = 0;
  virtual int
  create(const std::string& path, mode_t mode, int pflags, size_t align, off_t initialsz, size_t nproducts) = 0;
  virtual int
  close() = 0;
  virtual int
  insert(const Product& prod) = 0;
  virtual int
  newElement(const ProdInfo& info, std::unique_ptr<IQueueEntry>& outEntry) = 0;
  virtual int
  newElementDirect(const size_t size, const Signature& signature, std::unique_ptr<IQueueEntry>& outEntry) = 0;
  virtual int
  deleteBySignature(const Signature& signature) = 0;
  virtual int
  release(const off_t offset) = 0;

  // Properties and Metrics
  virtual int
  getOldestCursor(Timestamp& oldestCursor) = 0;
  virtual long
  getPQECount() = 0;
  virtual size_t
  getMagic() const = 0;
  virtual size_t
  getDataSize() const = 0;
  virtual int
  getHighwater(off_t& highwaterBytes, size_t& maxProducts) = 0;
  virtual int
  getStats(size_t& nprods, size_t& nfree, size_t& nempty, size_t& nbytes, size_t& maxprods, size_t& maxfree,
    size_t& minempty, size_t& maxbytes, double& age_oldest, size_t& maxextent) = 0;
  virtual int
  dumpFreeExtents() = 0;
  virtual int
  getMinVirtResTimeMetrics(Timestamp& minVirtResTime, off_t& size, size_t& slots) = 0;
  virtual int
  clearMinVirtResTimeMetrics() = 0;
  virtual bool
  isFull() = 0;
  virtual size_t
  getSlotCount() const = 0;
  virtual std::string
  getPathname() = 0;
  virtual int
  getFlags() = 0;
  virtual int
  getPageSize() const = 0;
  virtual int
  getMostRecent(Timestamp& mostRecent) = 0;
  virtual const char *
  strerror(const int error) const = 0;
  virtual int
  clearWriteCount() = 0;
  virtual int
  getWriteCount(size_t& write_count) = 0;
};
}
