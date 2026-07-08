#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <iostream>

#include <spdlog/fmt/bundled/core.h>

#define FEEDTYPE_OK 0

namespace rdm {
class FeedType {
private:
  uint32_t value_{ 0 };

public:
  constexpr
  FeedType() noexcept = default;
  constexpr FeedType(uint32_t val) noexcept : value_(val){ }

  constexpr uint32_t
  GetValue() const noexcept { return value_; }

  constexpr FeedType
  operator | (const FeedType& rhs) const noexcept { return FeedType(value_ | rhs.value_); }

  constexpr FeedType
  operator & (const FeedType& rhs) const noexcept { return FeedType(value_ & rhs.value_); }

  constexpr FeedType
  operator ^ (const FeedType& rhs) const noexcept { return FeedType(value_ ^ rhs.value_); }

  constexpr FeedType
  operator ~ () const noexcept { return FeedType(~value_); }

  FeedType&
  operator |= (const FeedType& rhs) noexcept { value_ |= rhs.value_; return *this; }

  FeedType&
  operator &= (const FeedType& rhs) noexcept { value_ &= rhs.value_; return *this; }

  FeedType&
  operator ^= (const FeedType& rhs) noexcept { value_ ^= rhs.value_; return *this; }

  constexpr bool
  operator == (const FeedType& rhs) const noexcept { return value_ == rhs.value_; }

  constexpr bool
  operator != (const FeedType& rhs) const noexcept { return value_ != rhs.value_; }

  constexpr explicit
  operator bool () const noexcept { return value_ != 0; }

  constexpr
  operator uint32_t () const noexcept { return value_; }

  std::string
  ToString() const;

  static int
  Parse(const std::string& expr, FeedType& result);
  static std::string
  GetParseErrorMsg(int errCode);

  friend std::istream&
  operator >> (std::istream& is, FeedType& ft)
  {
    uint32_t raw;

    if (is >> raw) {
      ft.value_ = raw;
    }
    return is;
  }

  friend std::ostream&
  operator << (std::ostream& os, const FeedType& ft)
  {
    return os << ft.value_;
  }
};

static_assert(std::is_trivially_copyable_v<FeedType> && std::is_standard_layout_v<FeedType>,
  "FeedType must remain a POD type for zero-copy mmap and RPC compatibility");

constexpr FeedType NONE      = 0;
constexpr FeedType FT0       = 1;
constexpr FeedType PPS       = 1;
constexpr FeedType FT1       = 2;
constexpr FeedType DDS       = 2;
constexpr FeedType DDPLUS    = 3;
constexpr FeedType FT2       = 4;
constexpr FeedType HDS       = 4;
constexpr FeedType HRS       = 4;
constexpr FeedType FT3       = 8;
constexpr FeedType IDS       = 8;
constexpr FeedType INTNL     = 8;
constexpr FeedType FT4       = 16;
constexpr FeedType SPARE     = 16;
constexpr FeedType WMO       = 15;
constexpr FeedType FT5       = 32;
constexpr FeedType UNIWISC   = 32;
constexpr FeedType MCIDAS    = 32;
constexpr FeedType UNIDATA   = 47;
constexpr FeedType FT6       = 64;
constexpr FeedType PCWS      = 64;
constexpr FeedType ACARS     = 64;
constexpr FeedType FT7       = 128;
constexpr FeedType FSL2      = 128;
constexpr FeedType PROFILER  = 128;
constexpr FeedType FT8       = 256;
constexpr FeedType FSL3      = 256;
constexpr FeedType FT9       = 512;
constexpr FeedType FSL4      = 512;
constexpr FeedType FT10      = 1024;
constexpr FeedType FSL5      = 1024;
constexpr FeedType FSL       = 1984;
constexpr FeedType FT11      = 2048;
constexpr FeedType AFOS      = 2048;
constexpr FeedType GPSSRC    = 2048;
constexpr FeedType FT12      = 4096;
constexpr FeedType CONDUIT   = 4096;
constexpr FeedType NMC2      = 4096;
constexpr FeedType NCEPH     = 4096;
constexpr FeedType FT13      = 8192;
constexpr FeedType NMC3      = 8192;
constexpr FeedType FNEXRAD   = 8192;
constexpr FeedType NMC       = 14336;
constexpr FeedType FT14      = 16384;
constexpr FeedType NLDN      = 16384;
constexpr FeedType FT15      = 32768;
constexpr FeedType WSI       = 32768;
constexpr FeedType FT16      = 65536;
constexpr FeedType DIFAX     = 65536;
constexpr FeedType SATELLITE = 65536;
constexpr FeedType FT17      = 131072;
constexpr FeedType FAA604    = 131072;
constexpr FeedType FT18      = 262144;
constexpr FeedType GPS       = 262144;
constexpr FeedType FT19      = 524288;
constexpr FeedType SEISMIC   = 524288;
constexpr FeedType NOGAPS    = 524288;
constexpr FeedType FNMOC     = 524288;
constexpr FeedType FT20      = 1048576;
constexpr FeedType CMC       = 1048576;
constexpr FeedType GEM       = 1048576;
constexpr FeedType FT21      = 2097152;
constexpr FeedType NIMAGE    = 2097152;
constexpr FeedType IMAGE     = 2097152;
constexpr FeedType FT22      = 4194304;
constexpr FeedType NTEXT     = 4194304;
constexpr FeedType TEXT      = 4194304;
constexpr FeedType FT23      = 8388608;
constexpr FeedType NGRID     = 8388608;
constexpr FeedType GRID      = 8388608;
constexpr FeedType FT24      = 16777216;
constexpr FeedType NPOINT    = 16777216;
constexpr FeedType POINT     = 16777216;
constexpr FeedType NBUFR     = 16777216;
constexpr FeedType BUFR      = 16777216;
constexpr FeedType FT25      = 33554432;
constexpr FeedType NGRAPH    = 33554432;
constexpr FeedType GRAPH     = 33554432;
constexpr FeedType FT26      = 67108864;
constexpr FeedType NOTHER    = 67108864;
constexpr FeedType OTHER     = 67108864;
constexpr FeedType NPORT     = 130023424;
constexpr FeedType FT27      = 134217728;
constexpr FeedType NEXRAD3   = 134217728;
constexpr FeedType NNEXRAD   = 134217728;
constexpr FeedType NEXRAD    = 134217728;
constexpr FeedType FT28      = 268435456;
constexpr FeedType CRAFT     = 268435456;
constexpr FeedType NEXRD2    = 268435456;
constexpr FeedType NEXRAD2   = 268435456;
constexpr FeedType FT29      = 536870912;
constexpr FeedType NXRDSRC   = 536870912;
constexpr FeedType FT30      = 1073741824;
constexpr FeedType EXP       = 0x40000000;
constexpr FeedType ANY       = 0xffffffff;
}

template <>
struct fmt::formatter<rdm::FeedType> {
  constexpr auto
  parse(fmt::format_parse_context& ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const rdm::FeedType& ft, FormatContext& ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", ft.ToString());
  }
};
