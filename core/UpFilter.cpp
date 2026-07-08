#include "UpFilter.h"

#include <stdexcept>

namespace rdm {

void UpFilter::AddComponent(FeedType feedtype, const Pattern& okPattern, const Pattern* notPattern) {
    for (const auto& elt : elements_) {
        if (feedtype & elt->ft) {
            throw std::invalid_argument("Feedtype " + feedtype.ToString() + 
                " overlaps with feedtype " + elt->ft.ToString());
        }
    }

    std::unique_ptr<Pattern> clonedNot;
    if (notPattern) {
        clonedNot = std::make_unique<Pattern>(*notPattern);
    }

    elements_.push_back(std::make_unique<Element>(feedtype, okPattern, std::move(clonedNot)));
    stringOutOfDate_ = true;
}

bool UpFilter::IsMatch(const ProdInfo& info) const {
    for (const auto& elt : elements_) {
        if (elt->ft & info.feedtype) {
            bool okMatch = elt->okPattern.isMatch(info.ident);
            bool notMatch = (elt->notPattern != nullptr) && elt->notPattern->isMatch(info.ident);
            return okMatch && !notMatch;
        }
    }
    return false;
}

size_t UpFilter::GetComponentCount() const {
    return elements_.size();
}

const std::string& UpFilter::ToString() const {
    if (stringOutOfDate_) {
        std::string s = "{";
        bool first = true;
        for (const auto& elt : elements_) {
            if (!first) s += ", ";
            first = false;
            s += "{";
            s += elt->ft.ToString();
            s += ", (";
            s += elt->okPattern.getEre();
            s += ")";
            if (elt->notPattern) {
                s += " - (";
                s += elt->notPattern->getEre();
                s += ")";
            }
            s += "}";
        }
        s += "}";
        cachedString_ = std::move(s);
        stringOutOfDate_ = false;
    }
    return cachedString_;
}

}
