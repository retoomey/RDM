#include "Pattern.h"
#include "Log.h"
#include <cstring>

namespace rdm {

std::string Pattern::sanitizeExpression(std::string_view expr) {
    std::string_view working = expr;
    bool adjusted = false;

    // Trim nested leading ".*" without falling out of bounds
    while (working.size() > 2 && working.substr(0, 2) == ".*") {
        working.remove_prefix(2);
        adjusted = true;
    }

    if (adjusted) {
        LogWarning("Adjusting pathological regular-expression: \"{}\" -> \"{}\"", expr, working);
    }

    return std::string(working);
}

Pattern::Pattern(std::string_view expr, bool ignoreCase, bool captures)
    : ignoreCase_(ignoreCase), 
      isMatchAll_(expr == ".*"), 
      captures_(captures) 
{
    // If it's a pure catch-all match, bypass full regex compilation overhead
    if (isMatchAll_) {
        ere_ = ".*";
        return;
    }

    // Natively clean up the pattern expression upon instantiation
    ere_ = sanitizeExpression(expr);

    auto flags = std::regex_constants::extended;
    if (!captures_) flags |= std::regex_constants::nosubs;
    if (ignoreCase_) flags |= std::regex_constants::icase;

    reg_.assign(ere_, flags);
}

bool Pattern::isMatch(const std::string& target) const {
    if (isMatchAll_) return true;
    try {
        return std::regex_search(target, reg_);
    } catch (const std::regex_error&) {
        return false;
    }
}

bool Pattern::extract(const std::string& target, std::vector<std::string>& matches) const {
    if (isMatchAll_) {
        matches.clear();
        matches.push_back(target); // Index 0 is always the full match
        return true;
    }

    try {
        std::smatch sm;
        if (std::regex_search(target, sm, reg_)) {
            matches.clear();
            matches.reserve(sm.size());
            for (const auto& m : sm) {
                matches.push_back(m.str());
            }
            return true;
        }
    } catch (const std::regex_error& e) {
        LogError("Regex extraction error on target '{}': {}", target, e.what());
    }
    
    return false;
}

}
