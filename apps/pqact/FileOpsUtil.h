#pragma once

#include "ProdInfo.h"
#include "Product.h"

#include <vector>
#include <cstdint>
#include <cstddef>

namespace rdm {
namespace pqact {
// A lightweight, non-owning view of a binary buffer.
struct BufferView {
  const uint8_t * data;
  size_t          size;
};

class FileOpsUtil {
public:

  /**
   * Shifts the buffer view past any SBN and WMO headers.
   * Does NOT allocate memory.
   */
  static BufferView
  StripHeaders(const uint8_t * data, size_t sz);

  /**
   * Creates a new, managed buffer with control characters stripped out.
   */
  static std::vector<uint8_t>
  DupStrip(const uint8_t * in, size_t len);

  static std::vector<uint8_t>
  SerializeMetadata(const ProdInfo& info, uint32_t sz);

  static BufferView
  PreparePayload(const Product& prod, int entryFlags, std::vector<uint8_t>& scratchpad);
};
} // namespace pqact
}
