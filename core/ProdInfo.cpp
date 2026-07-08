#include "ProdInfo.h"
#include <spdlog/fmt/fmt.h>

namespace rdm {

bool ProdInfo::operator==(const ProdInfo& other) const {
    return feedtype == other.feedtype &&
           seqno == other.seqno &&
           sz == other.sz &&
           arrival == other.arrival &&   // <-- Now uses the overloaded operator==
           signature == other.signature &&
           origin == other.origin &&
           ident == other.ident;
}

std::string ProdInfo::ToString(bool includeSig) const {
    std::string timeStr = arrival.ToString();
    if (includeSig) {
        return fmt::format("{} {:>10} {} {} {:03}  {}",
            signature.ToString(), sz, timeStr, feedtype, seqno, ident);
    } else {
        return fmt::format("{:>10} {} {} {:03}  {}",
            sz, timeStr, feedtype, seqno, ident);
    }
}
}
