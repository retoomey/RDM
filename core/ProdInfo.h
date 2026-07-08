#pragma once

#include "FeedType.h"
#include "Timestamp.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include <Signature.h>
#include <spdlog/fmt/bundled/core.h>

#define HOSTNAMESIZE 64

namespace rdm {
class ProdInfo {
public:
  Timestamp arrival{ 0, 0 };
  Signature signature{ };
  std::string origin;
  FeedType feedtype{ 0 };
  uint32_t seqno{ 0 };
  std::string ident;
  uint32_t sz{ 0 };

  ProdInfo()  = default;
  ~ProdInfo() = default;

  Timestamp
  GetArrival() const { return arrival; }

  const Signature&
  GetSignature() const { return signature; }

  const std::string&
  GetOrigin() const { return origin; }

  FeedType
  GetFeedtype() const { return feedtype; }

  uint32_t
  GetSeqno() const { return seqno; }

  const std::string&
  GetIdent() const { return ident; }

  uint32_t
  GetSize() const { return sz; }

  void SetArrival(Timestamp arr){ arrival = arr; }

  void SetSignature(const Signature& sig){ signature = sig; }

  void SetOrigin(const std::string& orig){ origin = orig; }

  void SetFeedtype(FeedType ft){ feedtype = ft; }

  void SetSeqno(uint32_t sn){ seqno = sn; }

  void SetIdent(const std::string& id){ ident = id; }

  void SetSize(uint32_t s){ sz = s; }

  bool
  operator == (const ProdInfo& other) const;
  std::string
  ToString(bool includeSig = true) const;
};
}

template <>
struct fmt::formatter<rdm::ProdInfo> {
  constexpr auto
  parse(format_parse_context& ctx) -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template <typename FormatContext>
  auto
  format(const rdm::ProdInfo& info, FormatContext& ctx) const -> decltype(ctx.out())
  {
    return fmt::format_to(ctx.out(), "{}", info.ToString());
  }
};
