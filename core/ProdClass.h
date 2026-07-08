#pragma once

#include "FeedType.h"
#include "ProdInfo.h"
#include "ProdSpec.h"

#include <vector>

#include <spdlog/fmt/bundled/core.h>

namespace rdm {
class ProdClass {
public:
  int64_t from_sec{ 0 };
  int32_t from_usec{ 0 };
  int64_t to_sec{ 0 };
  int32_t to_usec{ 0 };
  std::vector<ProdSpec> specs;

  ProdClass()  = default;
  ~ProdClass() = default;

  bool
  operator == (const ProdClass& rhs) const;

  bool
  Contains(const ProdInfo& info) const;

  void
  Optimize();

  bool
  Intersect(const ProdClass& want, ProdClass& result) const;

  FeedType
  GetUnionFeedtype() const;

  std::string
  ToString() const;
};
}

template <>
struct fmt::formatter<rdm::ProdClass> {
  constexpr auto
  parse(fmt::format_parse_context& ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const rdm::ProdClass& clss, FormatContext& ctx) const -> decltype(ctx.out())
  {
    // This automatically calls your ToString() method under the hood
    return fmt::format_to(ctx.out(), "{}", clss.ToString());
  }
};
