#pragma once

#include <string>
#include <spdlog/fmt/bundled/core.h>
#include "FeedType.h"

namespace rdm {
class ProdSpec {
public:
  FeedType feedtype{ 0 };
  std::string pattern;

  ProdSpec() = default;
  ProdSpec(FeedType ft, std::string pat)
    : feedtype(ft), pattern(std::move(pat)){ }

  ~ProdSpec() = default;

  bool
  operator == (const ProdSpec& rhs) const;

  std::string
  ToString() const;
};
}

template <>
struct fmt::formatter<rdm::ProdSpec> {
  constexpr auto
  parse(fmt::format_parse_context& ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const rdm::ProdSpec& spec, FormatContext& ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", spec.ToString());
  }
};
