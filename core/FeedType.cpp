#include "FeedType.h"
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <cstdio>

namespace rdm {

namespace {
    // Encapsulated parser constants
    constexpr int PARSE_OK        = 0;
    constexpr int PARSE_ERR_RP    = 1;
    constexpr int PARSE_ERR_PRIM  = 2;
    constexpr int PARSE_ERR_GARB  = 3;
    constexpr int PARSE_ERR_UKFT  = 4;

    struct FeedDef {
        const char* name;
        uint32_t type;
    };

    // This array is kept ordered intentionally to reproduce the 
    // exact bitmask-to-string decoding logic of the original ft_format.
    const std::vector<FeedDef> FEED_DEFS = {
        {"none",      NONE}, {"pps",       FT0}, {"dds",       FT1},
        {"ddplus",    DDPLUS}, {"hds",       FT2}, {"ids",       FT3},
        {"spare",     FT4}, {"wmo",       WMO}, {"uniwisc",   FT5},
        {"unidata",   UNIDATA}, {"pcws",      FT6}, {"fsl2",      FT7},
        {"fsl3",      FT8}, {"fsl4",      FT9}, {"fsl5",      FT10},
        {"fsl",       FSL}, {"gpssrc",    FT11}, {"conduit",   FT12},
        {"fnexrad",   FT13}, {"nmc",       NMC}, {"lightning", FT14},
        {"wsi",       FT15}, {"satellite", FT16}, {"faa604",    FT17},
        {"gps",       FT18}, {"fnmoc",     FT19}, {"gem",       FT20},
        {"nimage",    FT21}, {"ntext",     FT22}, {"ngrid",     FT23},
        {"npoint",    FT24}, {"ngraph",    FT25}, {"nother",    FT26},
        {"nport",     NPORT}, {"nexrad3",   FT27}, {"nexrad2",   FT28},
        {"nxrdsrc",   FT29}, {"exp",       FT30}, {"any",       ANY},
        {"ft0",       FT0}, {"domestic",  FT1}, {"ft1",       FT1},
        {"hrs",       FT2}, {"ft2",       FT2}, {"intnl",     FT3},
        {"ft3",       FT3}, {"nps",       FT4}, {"ft4",       FT4},
        {"ft5",       FT5}, {"mcidas",    FT5}, {"acars",     FT6},
        {"ft6",       FT6}, {"profiler",  FT7}, {"ft7",       FT7},
        {"ft8",       FT8}, {"ft9",       FT9}, {"ft10",      FT10},
        {"profs",     FSL}, {"afos",      FT11}, {"nmc1",      FT11},
        {"ft11",      FT11}, {"nmc2",      FT12}, {"nceph",     FT12},
        {"ft12",      FT12}, {"nmc3",      FT13}, {"ft13",      FT13},
        {"ft14",      FT14}, {"nldn",      FT14}, {"ft15",      FT15},
        {"difax",     FT16}, {"ft16",      FT16}, {"faa",       FT17},
        {"604",       FT17}, {"ft17",      FT17}, {"ft18",      FT18},
        {"nogaps",    FT19}, {"seismic",   FT19}, {"ft19",      FT19},
        {"cmc",       FT20}, {"ft20",      FT20}, {"image",     FT21},
        {"ft21",      FT21}, {"text",      FT22}, {"ft22",      FT22},
        {"grid",      FT23}, {"ft23",      FT23}, {"point",     FT24},
        {"nbufr",     FT24}, {"bufr",      FT24}, {"ft24",      FT24},
        {"graph",     FT25}, {"ft25",      FT25}, {"other",     FT26},
        {"ft26",      FT26}, {"nnexrad",   FT27}, {"nexrad",    FT27},
        {"ft27",      FT27}, {"nexrd2",    FT28}, {"craft",     FT28},
        {"ft28",      FT28}, {"ft29",      FT29}, {"ft30",      FT30}
    };

    // Fast mapping for parser lookups
    const std::unordered_map<std::string, uint32_t>& GetFeedMap() {
        static std::unordered_map<std::string, uint32_t> map;
        if (map.empty()) {
            for (const auto& def : FEED_DEFS) {
                map[def.name] = def.type;
            }
        }
        return map;
    }

    // --- Parser Engine ---
    enum class TokenType { NAME, END, UNION = '|', DIFF = '-', COMPLEMENT = '~', LP = '(', RP = ')' };

    struct ParseState {
        int err = PARSE_OK;
        TokenType tok = TokenType::END;
        std::string name;
        const char* cp = nullptr;
    };

    TokenType AdvanceToken(ParseState& ps) {
        if (ps.err) return TokenType::END;
        while (ps.cp && std::isspace(static_cast<unsigned char>(*ps.cp))) ps.cp++;
        
        if (!ps.cp || *ps.cp == '\0') return ps.tok = TokenType::END;

        switch (*ps.cp) {
            case '|':
            case '+': ps.cp++; return ps.tok = TokenType::UNION;
            case '-': ps.cp++; return ps.tok = TokenType::DIFF;
            case '(': ps.cp++; return ps.tok = TokenType::LP;
            case ')': ps.cp++; return ps.tok = TokenType::RP;
            case '~': ps.cp++; return ps.tok = TokenType::COMPLEMENT;
            default:
                ps.name.clear();
                while (std::isalnum(static_cast<unsigned char>(*ps.cp))) {
                    ps.name += *ps.cp++;
                }
                return ps.tok = TokenType::NAME;
        }
    }

    uint32_t ParseExpr(ParseState& ps);

    uint32_t ParsePrimary(ParseState& ps) {
        if (ps.err) return 0;
        uint32_t expr;
        switch (ps.tok) {
            case TokenType::NAME: {
                std::string lower_str = ps.name;
                std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                AdvanceToken(ps);
                
                auto& map = GetFeedMap();
                auto it = map.find(lower_str);
                if (it != map.end()) return it->second;
                
                ps.err = PARSE_ERR_UKFT;
                return 0;
            }
            case TokenType::COMPLEMENT:
                AdvanceToken(ps);
                return ~ParsePrimary(ps);
            case TokenType::LP:
                AdvanceToken(ps);
                expr = ParseExpr(ps);
                if (ps.tok != TokenType::RP) {
                    ps.err = PARSE_ERR_RP;
                    return 0;
                }
                AdvanceToken(ps);
                return expr;
            default:
                ps.err = PARSE_ERR_PRIM;
                return 0;
        }
    }

    uint32_t ParseTerm(ParseState& ps) {
        uint32_t left = ParsePrimary(ps);
        if (ps.err) return 0;
        for (;;) {
            if (ps.tok == TokenType::DIFF) {
                AdvanceToken(ps);
                left &= ~ParsePrimary(ps);
            } else {
                return left;
            }
        }
    }

    uint32_t ParseExpr(ParseState& ps) {
        uint32_t left = ParseTerm(ps);
        if (ps.err) return 0;
        for (;;) {
            if (ps.tok == TokenType::UNION) {
                AdvanceToken(ps);
                if (ps.err) return 0;
                left |= ParseTerm(ps);
            } else {
                return left;
            }
        }
    }
}

std::string FeedType::ToString() const {
    if (value_ == NONE.value_) return "NONE";

    std::string s;
    uint32_t remaining = value_;
    
    // Find 'ANY' index to start backward traversal
    int idx = 0;
    while (idx < FEED_DEFS.size() && FEED_DEFS[idx].type != ANY.value_) {
        idx++;
    }

    while (remaining > 0 && idx >= 0 && FEED_DEFS[idx].type != NONE.value_) {
        if ((FEED_DEFS[idx].type & remaining) == FEED_DEFS[idx].type) {
            if (!s.empty()) s += "|";
            
            std::string name = FEED_DEFS[idx].name;
            for (auto& c : name) c = std::toupper(c);
            s += name;
            
            remaining &= ~FEED_DEFS[idx].type;
        }
        idx--;
    }

    // Append raw hex if there are unmatched bits
    if (remaining > 0) {
        char hex[32];
        std::snprintf(hex, sizeof(hex), s.empty() ? "0x%08x" : "|0x%08x", remaining);
        s += hex;
    }

    return s;
}

int FeedType::Parse(const std::string& str, FeedType& result) {
    if (str.empty()) return PARSE_ERR_PRIM;
    
    ParseState ps;
    ps.cp = str.c_str();
    AdvanceToken(ps);
    
    uint32_t parsedVal = ParseExpr(ps);
    
    if (ps.err == PARSE_OK && ps.tok != TokenType::END) {
        ps.err = PARSE_ERR_GARB;
    }
    
    if (ps.err == PARSE_OK) {
        result = FeedType(parsedVal);
    }
    
    return ps.err;
}

std::string FeedType::GetParseErrorMsg(int errCode) {
    switch (errCode) {
        case PARSE_OK:       return "";
        case PARSE_ERR_RP:   return "missing right paren in feedtype expression";
        case PARSE_ERR_PRIM: return "bad syntax in feedtype expression";
        case PARSE_ERR_GARB: return "garbage at end of feedtype expression";
        case PARSE_ERR_UKFT: return "unknown feed name in feedtype expression";
        default:             return "error in feedtype expression";
    }
}

}
