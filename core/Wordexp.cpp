#include "Wordexp.h"
#include <cctype>
#include <stdexcept>

namespace rdm {

Wordexp::Wordexp(const std::string& words) {
    size_t pos = 0;
    
    // Skip leading whitespace
    while (pos < words.length() && std::isspace(static_cast<unsigned char>(words[pos]))) {
        pos++;
    }

    // If the very first non-whitespace character is '#', return empty (legacy behavior)
    if (pos < words.length() && words[pos] == '#') {
        buildArgv();
        return;
    }

    std::string currentToken;
    bool inQuotes = false;

    while (pos < words.length()) {
        char c = words[pos];

        if (inQuotes) {
            if (c == '"') {
                inQuotes = false;
            } else {
                currentToken += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (std::isspace(static_cast<unsigned char>(c))) {
                if (!currentToken.empty()) {
                    tokens_.push_back(currentToken);
                    currentToken.clear();
                }
            } else {
                // If it's a mid-line comment, we stop parsing immediately
                if (c == '#') {
                    break;
                }
                currentToken += c;
            }
        }
        pos++;
    }

    if (inQuotes) {
        throw std::invalid_argument("Unclosed quote in string");
    }

    if (!currentToken.empty()) {
        tokens_.push_back(currentToken);
    }

    buildArgv();
}

void Wordexp::buildArgv() {
    argv_.clear();
    argv_.reserve(tokens_.size() + 1);
    for (auto& token : tokens_) {
        argv_.push_back(const_cast<char*>(token.c_str()));
    }
    argv_.push_back(nullptr); // execvp() requires a null-terminated array
}

Wordexp::Wordexp(Wordexp&& other) noexcept 
    : tokens_(std::move(other.tokens_)) {
    buildArgv();
}

Wordexp& Wordexp::operator=(Wordexp&& other) noexcept {
    if (this != &other) {
        tokens_ = std::move(other.tokens_);
        buildArgv();
    }
    return *this;
}

Wordexp::Wordexp(const Wordexp& other) : tokens_(other.tokens_) {
    buildArgv();
}

Wordexp& Wordexp::operator=(const Wordexp& other) {
    if (this != &other) {
        tokens_ = other.tokens_;
        buildArgv();
    }
    return *this;
}

}
