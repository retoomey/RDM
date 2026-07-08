#include "ProdClass.h"
#include "Pattern.h"
#include <algorithm>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ranges.h> // Required for fmt::join

namespace rdm {

bool ProdClass::operator==(const ProdClass& rhs) const {
    if (from_sec != rhs.from_sec || from_usec != rhs.from_usec) return false;
    if (to_sec != rhs.to_sec || to_usec != rhs.to_usec) return false;
    
    if (specs.size() != rhs.specs.size()) return false;
    
    for (size_t i = 0; i < specs.size(); ++i) {
        if (specs[i].feedtype != rhs.specs[i].feedtype) return false;
        if (specs[i].pattern != rhs.specs[i].pattern) return false;
    }
    
    return true;
}

bool ProdClass::Contains(const ProdInfo& info) const {
    if (info.arrival.tv_sec < from_sec ||
        (info.arrival.tv_sec == from_sec && info.arrival.tv_usec < from_usec)) {
        return false;
    }
    
    if (info.arrival.tv_sec > to_sec ||
        (info.arrival.tv_sec == to_sec && info.arrival.tv_usec > to_usec)) {
        return false;
    }
    
    if (specs.empty()) return true;
    
    for (const auto& spec : specs) {
        if (info.feedtype & spec.feedtype) {
            if (spec.pattern.empty() || spec.pattern == ".*") {
                return true;
            }
            Pattern pat(spec.pattern);
            if (pat.isMatch(info.ident)) {
                return true;
            }
        }
    }
    
    return false;
}

void ProdClass::Optimize() {
    if (specs.empty()) return;
    
    std::vector<ProdSpec> valid_specs;
    valid_specs.reserve(specs.size()); // Slight optimization to avoid reallocations
    
    for (const auto& sp : specs) {
        if (sp.feedtype != NONE) {
            valid_specs.push_back(sp);
        }
    }
    
    specs = std::move(valid_specs);
}

bool ProdClass::Intersect(const ProdClass& want, ProdClass& result) const {
    if (specs.empty() && want.specs.empty()) {
        result = want;
        return true;
    }
    
    if (from_sec > want.to_sec || (from_sec == want.to_sec && from_usec > want.to_usec)) return false;
    if (want.from_sec > to_sec || (want.from_sec == to_sec && want.from_usec > to_usec)) return false;

    // Intersect the 'from' time (take the max)
    if (from_sec > want.from_sec || (from_sec == want.from_sec && from_usec > want.from_usec)) {
        result.from_sec = from_sec; 
        result.from_usec = from_usec;
    } else {
        result.from_sec = want.from_sec; 
        result.from_usec = want.from_usec;
    }

    // Intersect the 'to' time (take the min)
    if (to_sec < want.to_sec || (to_sec == want.to_sec && to_usec < want.to_usec)) {
        result.to_sec = to_sec; 
        result.to_usec = to_usec;
    } else {
        result.to_sec = want.to_sec; 
        result.to_usec = want.to_usec;
    }

    bool filt_is_any = specs.empty() || (specs.size() == 1 && specs[0].feedtype == ANY && specs[0].pattern == ".*");
    if (filt_is_any) {
        result.specs = want.specs;
        return true;
    }

    result.specs.clear(); // Ensure result specs are clean before populating
    for (const auto& w_spec : want.specs) {
        for (const auto& f_spec : specs) {
            FeedType intersection = f_spec.feedtype & w_spec.feedtype;
            if (intersection != NONE) {
                if (f_spec.pattern == ".*" || f_spec.pattern == w_spec.pattern) {
                    result.specs.push_back({intersection, w_spec.pattern});
                }
            }
        }
    }
    
    return !result.specs.empty();
}

FeedType ProdClass::GetUnionFeedtype() const {
    FeedType ft = NONE;
    for (const auto& sp : specs) {
        ft |= sp.feedtype;
    }
    return ft;
}

std::string ProdClass::ToString() const {
    Timestamp from_ts(from_sec, static_cast<int32_t>(from_usec));
    Timestamp to_ts(to_sec, static_cast<int32_t>(to_usec));

    return fmt::format("{} {} {{{}}}", 
                       from_ts.ToString(), 
                       to_ts.ToString(), 
                       fmt::join(specs, ","));
}


}
