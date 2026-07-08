#pragma once
#include "FeedType.h"
#include "Product.h"
#include <cstddef>

namespace rdm {
/**
 * @brief Interface for Product Queue (Disk) Serialization.
 *
 * @warning DANGER: DISK VS NETWORK DIVERGENCE
 * This interface strictly governs how products are laid out in the local
 * memory-mapped product queue (disk). It DOES NOT govern network transit.
 * * To save disk space and I/O, the LDM disk layout intentionally deviates from
 * standard network XDR. Specifically, the raw data payload appended to the
 * end of the product is stored EXACTLY as-is. It omits the standard 4-byte
 * RPC/XDR length prefix, and it omits the standard 4-byte alignment padding.
 */
class IProductSerializer {
public:
  virtual
  ~IProductSerializer() = default;

  /**
   * @brief Calculates the exact byte footprint of the entire product on disk.
   * @note This must NOT include network padding or redundant length headers.
   */
  virtual size_t
  GetEncodedSize(const Product& prod) const = 0;

  /**
   * @brief Calculates the exact byte footprint of just the metadata header.
   */
  virtual size_t
  GetEncodedInfoSize(const ProdInfo& info) const = 0;

  /**
   * @brief Serializes the full product into the provided queue memory buffer.
   * @note The data payload must be appended directly after the metadata
   * with zero padding.
   */
  virtual bool
  EncodeProduct(void * buffer, size_t buffer_size, const Product& prod) const = 0;

  /**
   * @brief Deserializes the metadata header from the queue memory buffer.
   * @param next_ptr If provided, will be pointed to the exact start of the
   * unpadded, un-prefixed raw data payload.
   */
  virtual bool
  DecodeProdInfo(const void * buffer, size_t buffer_size, ProdInfo& info, void ** next_ptr = nullptr) const = 0;

  /**
   * @brief Serializes just the metadata header (used for piecemeal chunking).
   * @return A pointer to the memory location immediately following the header,
   * where the raw data should begin.
   */
  virtual void *
  EncodeProdInfo(void * buffer, size_t buffer_size, const ProdInfo& info) const = 0;

  virtual bool
  UpdateSignature(void * buffer, size_t buffer_size, const Signature& new_sig) const = 0;
};
}
