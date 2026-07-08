#include "ProdSpec.h"
#include <spdlog/fmt/fmt.h>

namespace rdm {

bool ProdSpec::operator==(const ProdSpec& rhs) const {
    return feedtype == rhs.feedtype && pattern == rhs.pattern;
}

std::string ProdSpec::ToString() const {
    return fmt::format("{{{}, \"{}\"}}",
        feedtype,
        pattern.empty() ? "(null)" : pattern);
}

}
