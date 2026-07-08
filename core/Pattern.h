#pragma once
#include <string>
#include <regex>
#include <vector>
#include <string_view>

namespace rdm {

/** Hide the regex matching for the system into this class.
 * It's possible the std::regex might slow the system, so we're
 * encapsulating it.  It would be easy enough then with this
 * 'one-stop-shop' to replace say with google's re2 library or
 * even the C functions which can be faster but less safe.  I'm
 * leaning safety over speed here.  We'll see how it works in
 * practice and refactor if needed. */
class Pattern {
private:
    std::string ere_;
    std::regex reg_;
    bool ignoreCase_;
    bool isMatchAll_;
    bool captures_;

    // Centralized, safe preprocessing helper
    static std::string sanitizeExpression(std::string_view expr);

public:
   /**
    * @brief Constructs a regular expression wrapper.
    * @param expr The POSIX Extended Regular Expression string.
    * @param ignoreCase If true, matches are case-insensitive.
    * @param captures If true, the internal engine tracks sub-expressions.
    * WARNING: Set to false for high-throughput boolean matching.
    */
    // Accept strings/literals cleanly via string_view
    explicit Pattern(std::string_view expr, bool ignoreCase = false, bool captures = false);
    
    Pattern(const Pattern&) = default;
    Pattern(Pattern&&) noexcept = default;
    Pattern& operator=(const Pattern&) = default;
    Pattern& operator=(Pattern&&) noexcept = default;

    /**
    * @brief Fast boolean check to see if the target matches the pattern.
    */
    bool isMatch(const std::string& target) const;

    /**
     * @brief Evaluates the pattern and extracts capturing groups.
     * @param target The string to evaluate.
     * @param matches A vector populated with the results. Index 0 is the full match,
     * Index 1 is the first capture group, etc.
     * @return true if the pattern matched the target, false otherwise.
     */
    bool extract(const std::string& target, std::vector<std::string>& matches) const;
    
    const std::string& getEre() const { return ere_; }
    bool isMatchAll() const { return isMatchAll_; }
};

}
