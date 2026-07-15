#include "AclManager.h"
#include "ProdInfo.h"
#include "Log.h"
#include <regex>

namespace rdm {

bool AclManager::RequiresServer() const {
    return !allowRules_.empty() || !acceptRules_.empty();
}

bool AclManager::IsHostMatch(const std::string& hostName, const std::string& ipAddr, const std::string& pattern) const {
    // Treat the pattern as a regular expression. 
    // This covers legacy HS_NAME, HS_DOTTED_QUAD, and HS_REGEXP.
    try {
        std::regex rgx(pattern, std::regex_constants::extended | std::regex_constants::icase);
        return std::regex_search(ipAddr, rgx) || std::regex_search(hostName, rgx);
    } catch (const std::regex_error&) {
        // Fallback to strict string equality if regex compilation fails
        return (ipAddr == pattern) || (hostName == pattern);
    }
}

bool AclManager::IsHostOk(const std::string& hostName, const std::string& ipAddr) const {
    for (const auto& rule : allowRules_) {
        if (IsHostMatch(hostName, ipAddr, rule.hostPattern)) return true;
    }
    for (const auto& rule : acceptRules_) {
        if (IsHostMatch(hostName, ipAddr, rule.hostPattern)) return true;
    }
    return false;
}

FeedType AclManager::GetAllowed(const std::string& hostName, const std::string& ipAddr, FeedType desiredFeed) const {
    FeedType accumulatedAllowed = 0; // NONE
    
    for (const auto& rule : allowRules_) {
        if (IsHostMatch(hostName, ipAddr, rule.hostPattern)) {
            accumulatedAllowed |= rule.feedtype;
        }
    }
    
    // Return the intersection of what they requested vs what they are allowed
    return desiredFeed & accumulatedAllowed;
}

int AclManager::ReduceToAllowed(const std::string& hostName, const std::string& ipAddr, const ProdClass& want, ProdClass& intersect) const {
    // 1. Get the master bitmask of what this host is allowed to see
    FeedType allowedFeed = GetAllowed(hostName, ipAddr, 0xffffffff /* ANY */);
    
    if (allowedFeed == rdm::NONE) {
        return 1; // 1 = Denied / No matching rules
    }

    // 2. Carry over the time constraints
    intersect.from_sec = want.from_sec;
    intersect.from_usec = want.from_usec;
    intersect.to_sec = want.to_sec;
    intersect.to_usec = want.to_usec;
    intersect.specs.clear();

    // 3. Intersect the requested feedtypes with the allowed feedtypes
    for (const auto& spec : want.specs) {
        FeedType reduced = spec.feedtype & allowedFeed;
        if (reduced != rdm::NONE) { // 0 = NONE
            ProdSpec newSpec;
            newSpec.feedtype = reduced;
            newSpec.pattern = spec.pattern;
            intersect.specs.push_back(newSpec);
        }
    }
    
    return intersect.specs.empty() ? 1 : 0;
}

int AclManager::ReduceToAcceptable(const std::string& hostName, const std::string& ipAddr, const ProdClass& offered, ProdClass& intersect) const {
    FeedType acceptedFeed = rdm::NONE;
    
    // Find matching ACCEPT rules for this host
    for (const auto& rule : acceptRules_) {
        if (IsHostMatch(hostName, ipAddr, rule.hostPattern)) {
            acceptedFeed |= rule.feedtype;
        }
    }

    if (acceptedFeed == rdm::NONE) {
        return 1; // No matching ACCEPT rule found
    }

    intersect.from_sec = offered.from_sec;
    intersect.from_usec = offered.from_usec;
    intersect.to_sec = offered.to_sec;
    intersect.to_usec = offered.to_usec;
    intersect.specs.clear();

    // Intersect the offered specs with the accepted feedtypes
    for (const auto& spec : offered.specs) {
        FeedType reduced = spec.feedtype & acceptedFeed;
        if (reduced != rdm::NONE) {
            ProdSpec newSpec;
            newSpec.feedtype = reduced;
            // Note: In a fully fleshed out version, we'd also intersect spec.pattern 
            // with rule.prodPattern, but keeping it simple for the feedtype intersection first.
            newSpec.pattern = spec.pattern; 
            intersect.specs.push_back(newSpec);
        }
    }
    
    return intersect.specs.empty() ? 1 : 0;
}

std::shared_ptr<UpFilter> AclManager::GetUpstreamFilter(const std::string& hostName, const std::string& ipAddr, const ProdClass& want) const {
    auto filt = std::make_shared<UpFilter>();
    
    for (const auto& spec : want.specs) {
        for (const auto& rule : allowRules_) {
            FeedType intersection = rule.feedtype & spec.feedtype;
            
            // If the feedtype overlaps AND the host matches this specific ALLOW rule
            if (intersection != rdm::NONE && IsHostMatch(hostName, ipAddr, rule.hostPattern)) {
                filt->AddComponent(intersection, rule.okPattern, rule.notPattern.get());
                break; // Rule matched, move to the next ProdSpec
            }
        }
    }

    if (filt->GetComponentCount() > 0) {
        return filt;
    }
    return nullptr;
}

}
