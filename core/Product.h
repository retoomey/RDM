#pragma once

#include "FeedType.h"
#include "ProdInfo.h"

#include <cstdint>
#include <cstring>

namespace rdm {
struct Product {
  ProdInfo        info;
  const uint8_t * data;

  bool
  operator == (const Product& other) const
  {
    if (!(info == other.info)) { return false; }
    if (info.sz > 0) {
      if (data == other.data) { return true; }
      if (!data || !other.data) { return false; }
      return std::memcmp(data, other.data, info.sz) == 0;
    }
    return true;
  }
};
}
