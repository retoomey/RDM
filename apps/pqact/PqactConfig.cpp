#include "PqactConfig.h"
#include "PqactContext.h"
#include "Wordexp.h"
#include "Log.h"
#include <cctype>
#include <cstring>

namespace rdm {
namespace pqact {

// Notice captures=true is passed to the Pattern constructor
PqactEntry::PqactEntry(FeedType ft, const std::string& pat, std::unique_ptr<IAction> act, std::string a)
    : feedtype(ft), prog(pat, false, true), action(std::move(act)), args(std::move(a)) {
}

static std::string applyRegSub(const std::string& args, const std::vector<std::string>& matches) {
    std::string result;
    result.reserve(args.length() + 64);
    for (size_t i = 0; i < args.length(); ++i) {
        if (args[i] == '\\' && i + 1 < args.length() && std::isdigit(args[i + 1])) {
            size_t no = args[++i] - '0';
            if (no < matches.size()) {
                result.append(matches[no]);
            }
        } else if (args[i] == '\\' && i + 2 < args.length() && args[i+1] == '(' && std::isdigit(args[i+2])) {
            size_t endParen = args.find(')', i + 2);
            if (endParen != std::string::npos) {
                size_t no = std::stoul(args.substr(i + 2, endParen - (i + 2)));
                if (no < matches.size()) {
                    result.append(matches[no]);
                }
                i = endParen;
            } else {
                result += args[i];
            }
        } else {
            result += args[i];
        }
    }
    return result;
}

static std::string applyDateSub(const std::string& input, time_t prodClock) {
    std::string result = input;
    struct tm utcProdTime;
    if (gmtime_r(&prodClock, &utcProdTime) == nullptr) return result;

    size_t pos = 0;
    while ((pos = result.find("(", pos)) != std::string::npos) {
        size_t endPos = result.find(")", pos);
        if (endPos == std::string::npos) break;

        std::string token = result.substr(pos + 1, endPos - pos - 1);
        size_t colon = token.find(':');
        if (colon != std::string::npos) {
            int dom = std::stoi(token.substr(0, colon));
            std::string fmt = token.substr(colon + 1);
            for (char& c : fmt) c = std::tolower(c);

            struct tm adjTime = utcProdTime;
            adjTime.tm_mday = dom > 0 ? dom : adjTime.tm_mday;
            mktime(&adjTime);

            char buf[16];
            if (fmt == "yyyy") std::snprintf(buf, sizeof(buf), "%04d", adjTime.tm_year + 1900);
            else if (fmt == "mm") std::snprintf(buf, sizeof(buf), "%02d", adjTime.tm_mon + 1);
            else if (fmt == "dd") std::snprintf(buf, sizeof(buf), "%02d", adjTime.tm_mday);
            else if (fmt == "hh") std::snprintf(buf, sizeof(buf), "%02d", adjTime.tm_hour);

            result.replace(pos, endPos - pos + 1, buf);
            pos += std::strlen(buf);
        } else {
            pos = endPos + 1;
        }
    }
    return result;
}

static std::string applySeqSub(std::string input, unsigned seqnum) {
    size_t pos = 0;
    while ((pos = input.find("(seq)", pos)) != std::string::npos) {
        std::string replacement = std::to_string(seqnum);
        input.replace(pos, 5, replacement);
        pos += replacement.length();
    }
    return input;
}

int PqactEntry::Execute(const Product& prod, const void* xprod, size_t xlen) {
    if (args.empty()) {
        std::vector<std::string> emptyArgs;
        return action->Execute(prod, emptyArgs, xprod, xlen);
    }

    std::vector<std::string> matches;
    prog.extract(prod.info.ident, matches);

    std::string processedArgs = applyRegSub(args, matches);
    processedArgs = applyDateSub(processedArgs, prod.info.arrival.tv_sec);
    processedArgs = applySeqSub(processedArgs, prod.info.seqno);

    LogDebug("{}: {{cmd: \"{}\", ident: \"{}\"}}", action->GetName(), processedArgs, prod.info.ident);

    try {
        Wordexp we(processedArgs);
        return action->Execute(prod, we.getTokens(), xprod, xlen);
    } catch (const std::exception& e) {
        LogError("Argument tokenization failed: {}", e.what());
        return -1;
    }
}

void PqactConfig::ProcessProduct(const Product& prod, PqactContext& ctx, const void* xprod, size_t xlen, bool& didMatch, bool& errorOccurred) {
    didMatch = false;
    errorOccurred = false;

    auto it = entries.begin();
    while (it != entries.end()) {
        PqactEntry* entry = it->get();
        bool nodeRemoved = false;

        if (prod.info.feedtype & entry->feedtype) {
            bool regexMatched = entry->prog.isMatch(prod.info.ident);
            bool elseMatched = (entry->prog.getEre() == "^_ELSE_$" && !didMatch && prod.info.ident[0] != '_');

            if (regexMatched || elseMatched) {
                didMatch = true;
                if (entry->Execute(prod, xprod, xlen)) {
                    if (entry->action->IsTransient()) {
                        it = entries.erase(it);
                        nodeRemoved = true;
                    }
                    errorOccurred = true;
                }
            }
        }

        if (!nodeRemoved) {
            ++it;
        }
    }
}

}
}
