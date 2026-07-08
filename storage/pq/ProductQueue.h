#pragma once

#include "IProductStore.h"
#include "IProductSerializer.h"
#include "RegionManager.h"
#include "QueueIndexManager.h"
#include "QueueEntry.h"
#include "ProdClass.h"
#include "Signature.h"
#include "PqLocks.h"
#include "PqFile.h"
#include "QueueMetrics.h"

#include <memory>
#include <atomic>
#include <limits>
#include <climits>
#include <unordered_map>

#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>

constexpr size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();

namespace rdm {

class QueueCursor;
struct SignatureElement;
struct TimeQueueElement;

/**
 * @class ProductQueue
 * @brief Modern C++ RAII Wrapper and Facade for the legacy LDM memory-mapped pqueue subsystem.
 *
 * This class encapsulates the lifetime, resource mapping, and iteration mechanics
 * of the disk-backed circular product queue buffer. It eliminates manual lifecycle
 * errors by leveraging RAII semantics to guarantee safe unmapping and lock release
 * during unexpected execution unwinding or normal scope termination.
 *
 * @author Robert Toomey
 * @date May 2026
 */
class ProductQueue : public IProductStore {
  friend class QueueEntry;
  friend class ControlLock;
  friend class QueueCursor;
private:
  std::shared_ptr<IProductSerializer> serializer_;
  std::unique_ptr<RegionManager> regionManager_;
  std::unique_ptr<MappedRegion> ctlRegion_;
  std::unique_ptr<MappedRegion> idxRegion_;
  std::atomic<long> pqe_count_{ 0 };
  std::atomic<long> locked_count_{ 0 };
  std::unordered_map<off_t, std::unique_ptr<MappedRegion> > activeUserRegions_;
  int pflags_{ 0 };
//  size_t pagesz_{ 0 };
//  int fd_{ -1 };
//  std::string pathname_;
  PqFile file_;
  off_t dataOffset_{ 0 };
  off_t indexOffset_{ 0 };
  size_t indexSize_{ 0 };
  size_t maxSlots_{ 0 };
  QueueIndexManager indexManager_;
  mutable std::unique_ptr<PqMutex> mutex_; // eh

  bool isOpen() const { return file_.isOpen(); }

  int
  commitEntry(const pqe_index& index, std::unique_ptr<MappedRegion> region);
  int
  rollbackEntry(const pqe_index& index, std::unique_ptr<MappedRegion> region);

  off_t
  total_size() const { return static_cast<off_t>(indexOffset_ + indexSize_); }

  int
  initQueue(int pflags, size_t align, off_t initialsz, size_t maxProds);

  int
  formatControlRegion(size_t align);
  int
  loadControlRegion(const std::string& path);
  bool
  isProductMappingNecessary() const;

public:
  explicit
  ProductQueue(std::shared_ptr<IProductSerializer> serializer);

  explicit
  ProductQueue(const std::string& path, const int flags,
    std::shared_ptr<IProductSerializer> serializer);

  ~ProductQueue();

  ProductQueue(const ProductQueue&) = delete;
  ProductQueue&
  operator = (const ProductQueue&) = delete;
  ProductQueue(ProductQueue&&)     = delete;
  ProductQueue&
  operator = (ProductQueue&&) = delete;

  std::unique_ptr<IQueueCursor>
  CreateCursor() override;

  /** Binds a physical product-queue file on disk to a process
   * virtual memory space. */
  int
  open(const std::string& path, const int flags);

  int
  create(const std::string& path, mode_t mode, int pflags, size_t align, off_t initialsz, size_t nproducts);

  /**
   * @brief Gracefully detaches the virtual memory mapping and decrements active writer counters.
   * @retval 0         Success.
   * @retval EOVERFLOW Writer counts fell below zero prematurely.
   */
  int
  close();

  /**
   * @brief Atomically appends a completed product frame to the tail-end of the queue.
   * @param[in] prod Ref-bounded data product container to commit.
   * @retval    0    Success. Inter-process readers are alerted via SIGCONT.
   * @retval    EACCES Room could not be reclaimed or queue is marked read-only.
   * @retval    PQ_DUP Unique cryptographic signature already exists in the catalog.
   * @retval    PQ_BIG Product payload bounds exceed max queue limitations.
   */
  int
  insert(const Product& prod);

  int newElement(const ProdInfo& info, std::unique_ptr<IQueueEntry>& outEntry) override;
  int newElementDirect(const size_t size, const Signature& signature, std::unique_ptr<IQueueEntry>& outEntry) override;

  /** Get the PQE Count value.  Doesn't appear to be used */
  long
  getPQECount();

  /**
   * @brief Returns the insertion-timestamp of the oldest data-product in the queue.
   * @param[out] oldestCursor Reference to receive the oldest timestamp.
   * @retval    0             Success.
   * @retval    PQ_SYSTEM     System failure while reading control header.
   */
  int
  getOldestCursor(Timestamp& oldestCursor);

  /** Get the magic value.  Doesn't appear to be used */
  size_t
  getMagic();

  /**
   * @brief Returns the magic number found in the product-queue control block.
   * @retval 0  If the queue handle is uninitialized (nullptr).
   * @retval -1 If the virtual memory base mapping is null.
   * @return The validation magic number of the product queue file.
   */
  size_t
  getMagic() const;

  /**
   * Releases a data-product that was locked by `pq_sequenceLock()` so that it can
   * be deleted to make room for another product.
   */
  int
  release(const off_t offset);

  /**
   * Returns the size of the data portion of a product-queue.
   */
  size_t
  getDataSize() const;

  /**
   * @brief Gathers statistical highwater records compiled over the lifecycle of the queue.
   * @param[out] highwaterBytes Max bytes utilized by active product payloads.
   * @param[out] maxProducts    Peak parallel allocation count of metadata objects.
   * @retval     0              Success. Output variables populated.
   */
  int
  getHighwater(off_t& highwaterBytes, size_t& maxProducts);

  /**
   * Get some detailed product queue statistics.  These may
   * be useful for monitoring the internal state of the product
   * queue: */
  int
  getStats(size_t& nprods, size_t& nfree, size_t& nempty, size_t& nbytes,
    size_t& maxprods, size_t& maxfree, size_t& minempty, size_t& maxbytes,
    double& age_oldest, size_t& maxextent);

  int
  dumpFreeExtents();

  /*
   * Returns metrics associated with the minimum virtual residence time of
   * data-products in the queue since the queue was created or the metrics reset.
   * The virtual residence time of a data-product is the time that the product
   * was removed from the queue minus the time that the product was created.  The
   * minimum virtual residence time is the minimum of the virtual residence times
   * over all applicable products.
   *
   * Arguments:
   *      pq              Pointer to the product-queue structure.  Shall not be
   *                      NULL.
   *      minVirtResTime  Pointer to the minimum virtual residence time of the
   *                      queue since the queue was created.  Shall not be NULL.
   *                      "*minVirtResTime" is set upon successful return.  If
   *                      such a time doesn't exist (because no products have
   *                      been deleted from the queue, for example), then
   *                      "*minVirtResTime" shall be TS_NONE upon successful
   *                      return.
   *      size            Pointer to the amount of data used, in bytes, when the
   *                      minimum virtual residence time was set. Shall not be
   *                      NULL. Set upon successful return. If this parameter
   *                      doesn't exist, then "*size" shall be set to -1.
   *      slots           Pointer to the number of slots used when the minimum
   *                      virtual residence time was set. Shall not be NULL. Set
   *                      upon successful return. If this parameter doesn't exist,
   *                      the "*slots" shall be set to 0.
   * Returns:
   *      0               Success.  All the outout metrics are set.
   *      else            <errno.h> error code.
   */
  int
  getMinVirtResTimeMetrics(Timestamp& minVirtResTime, off_t& size, size_t& slots);

  int
  clearMinVirtResTimeMetrics();

  /*
   * Returns the number of pq_open()s for writing outstanding on an existing
   * product queue.  If a writing process terminates without calling pq_close(),
   * then the actual number will be less than this number.  This function opens
   * the product-queue read-only, so if there are no outstanding product-queue
   * writers, then the returned count will be zero.
   *
   * Arguments:
   *      path    The pathname of the product-queue.
   *      count   The memory to receive the number of writers.
   * Returns:
   *      0           Success.  *count will be the number of writers.
   *      EINVAL      path is NULL or count is NULL.  *count untouched.
   *      ENOSYS      Function not supported because product-queue doesn't support
   *                  writer-counting.
   *      PQ_CORRUPT  The  product-queue is internally inconsistent.
   *      else        <errno.h> error-code.  *count untouched.
   */
  int
  getWriteCount(size_t& count) override;

  /*
   * Sets to zero the number of pq_open()s for writing outstanding on the
   * product-queue.  This is a dangerous function and should only be used when
   * it is known that there are no outstanding pq_open()s for writing on the
   * product-queue.
   *
   * Arguments:
   *      path    The pathname of the product-queue.
   * Returns:
   *      0           Success.
   *      EINVAL      path is NULL.
   *      PQ_CORRUPT  The  product-queue is internally inconsistent.
   *      else        <errno.h> error-code.
   */
  int
  clearWriteCount() override;

  /**
   * @brief Evaluates whether the queue has reached stable density boundaries.
   * @return True if oldest products are currently being actively purged to fit arrivals.
   */
  bool
  isFull();

  /**
   * @brief Returns the total storage cell block capacity defined during creation.
   * @return Maximum size count of independent product indexes.
   */
  size_t
  getSlotCount() const;

  /**
   * Returns the pathname of a product-queue as given to `pq_create()` or
   * `pq_open()`.
   *
   * @return        The pathname of the product-queue as given to `pq_create()` or
   *                `pq_open()`.
   */
  std::string
  getPathname();

  /**
   * Returns the flags used to open or create a product-queue.
   *
   * @return        The flags: a bitwise OR of
   *                    PQ_MAPRGNS     Map region by region, default whole file.
   *                    PQ_NOCLOBBER   Don't replace an existing product-queue.
   *                    PQ_NOLOCK      Disable locking.
   *                    PQ_NOMAP       Use `malloc/read/write/free` instead of
   *                                   `mmap()`
   *                    PQ_PRIVATE     `mmap()` the file `MAP_PRIVATE`. Default is
   *                                   `MAP_SHARED`
   *                    PQ_READONLY    Default is read/write.
   *                    PQ_THREADSAFE  Product-queue access is thread-safe
   */
  int
  getFlags();

  int
  getPageSize() const;

  /**
   * Returns the time of the most-recent insertion of a data-product.
   * mostRecent           Pointer to the time of the most-recent insertion of a
   *                      data-product.  Upon successful return, "*mostRecent"
   *                      shall be TS_NONE if such a time doesn't exist (because
   *                      the queue is empty, for example).
   */
  int
  getMostRecent(Timestamp& mostRecent);

  /** Returns an appropriate error-message given a product-queue and error-code. */
  const char *
  strerror(const int error) const;

  /**
   * @brief Deletes a specific data product from the queue using its signature.
   * @param[in] signature The unique 128-bit MD5 checksum of the data product.
   * @retval    0          Success.
   * @retval    PQ_NOTFOUND The product does not exist in the queue.
   * @retval    PQ_LOCKED   The product is currently in use by another process.
   */
  int
  deleteBySignature(const Signature& signature);
};

}
