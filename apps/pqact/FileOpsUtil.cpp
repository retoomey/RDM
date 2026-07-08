#include "FileOpsUtil.h"
#include "IFileEntry.h"
#include "Log.h"

#include <cstring>
#include <cctype>
#include <algorithm>
#include <utility>

namespace rdm {

namespace pqact {

constexpr size_t WMO_TTAAII_LEN = 6;
constexpr size_t WMO_CCCC_LEN = 4;
constexpr size_t WMO_I2 = 5;
constexpr size_t WMO_BBB_LEN = 3;
constexpr size_t SIZE_SBN_HDR = 11;
constexpr size_t SIZE_SBN_TLR = 4;
constexpr size_t CHECK_DEPTH = 100;
constexpr size_t MIN_PRODUCT_SIZE = 21;

static std::pair<int, size_t> GetWmoOffset(const uint8_t* buf, size_t buflen) {
    const uint8_t* p_wmo = buf;
    int wmo_offset = -1;

    for (p_wmo = buf; p_wmo + WMO_I2 + 1 < buf + buflen; p_wmo++) {
        if (std::isalpha(p_wmo[0]) && std::isalpha(p_wmo[1]) &&
            std::isalpha(p_wmo[2]) && std::isalpha(p_wmo[3])) {
            if (std::isdigit(p_wmo[4]) && std::isdigit(p_wmo[5]) &&
               (std::isspace(p_wmo[6]) || std::isalpha(p_wmo[6]))) {
                wmo_offset = static_cast<int>(p_wmo - buf);
                p_wmo += 6;
                break;
            }
        } else if (std::memcmp(p_wmo, "\r\r\n", 3) == 0) {
            break;
        }
    }

    if (wmo_offset < 0) return {-1, 0};

    while (std::isspace(*p_wmo) && p_wmo < buf + buflen) p_wmo++;

    if (p_wmo + WMO_CCCC_LEN > buf + buflen) {
        return {-1, 0};
    } else if (std::isalpha(*p_wmo) && std::isalnum(*(p_wmo+1)) &&
               std::isalpha(*(p_wmo+2)) && std::isalnum(*(p_wmo+3))) {
        p_wmo += WMO_CCCC_LEN;
    } else {
        return {-1, 0};
    }

    while (std::isspace(*p_wmo) && p_wmo < buf + buflen) {
        p_wmo++;
    }

    if (p_wmo + 6 <= buf + buflen) {
        if (std::isdigit(*p_wmo) && std::isdigit(*(p_wmo+1)) &&
            std::isdigit(*(p_wmo+2)) && std::isdigit(*(p_wmo+3)) &&
            std::isdigit(*(p_wmo+4)) && std::isdigit(*(p_wmo+5))) {
            p_wmo += 6;
        }
    }

    int crcrlf_found = 0;
    int bbb_found = 0;
    while (p_wmo < buf + buflen) {
        if ((*p_wmo == '\r') || (*p_wmo == '\n')) {
            crcrlf_found++;
            p_wmo++;
            if (crcrlf_found == 3) break;
        } else if (crcrlf_found) {
            p_wmo--;
            break;
        } else if (std::isalpha(*p_wmo)) {
            if (bbb_found) return {wmo_offset, p_wmo - (buf + wmo_offset)};
            int i_bbb;
            for (i_bbb = 1; p_wmo + i_bbb < buf + buflen && i_bbb < WMO_BBB_LEN; i_bbb++) {
                if (!std::isalpha(p_wmo[i_bbb])) break;
            }
            if (p_wmo + i_bbb < buf + buflen && std::isspace(p_wmo[i_bbb])) {
                bbb_found = 1;
                p_wmo += i_bbb;
            } else {
                return {wmo_offset, p_wmo - (buf + wmo_offset)};
            }
        } else if (std::isspace(*p_wmo)) {
            p_wmo++;
        } else {
            return {wmo_offset, p_wmo - (buf + wmo_offset)};
        }
    }

    if (p_wmo + 9 <= buf + buflen) {
        if (std::isalnum(p_wmo[0]) && std::isalnum(p_wmo[1]) && std::isalnum(p_wmo[2]) &&
            std::isalnum(p_wmo[3]) && std::isalnum(p_wmo[4]) && std::isalnum(p_wmo[5]) &&
            (p_wmo[6] == '\r') && (p_wmo[7] == '\r') && (p_wmo[8] == '\n')) {
            p_wmo += 9;
        }
    }

    size_t wmo_len = p_wmo - (buf + wmo_offset);
    return {wmo_offset, wmo_len};
}

BufferView FileOpsUtil::StripHeaders(const uint8_t* data, size_t sz) {
    BufferView view{data, sz};
    if (view.size < MIN_PRODUCT_SIZE || !view.data) return view;

    size_t slen = std::min(view.size, CHECK_DEPTH);

    if (std::memcmp(view.data, "\001\015\015\012", 4) == 0 &&
        std::isdigit(view.data[4]) && std::isdigit(view.data[5]) && std::isdigit(view.data[6]) &&
        std::memcmp(&view.data[7], "\040\015\015\012", 4) == 0) {
        view.data += SIZE_SBN_HDR;
        view.size -= (SIZE_SBN_HDR + SIZE_SBN_TLR);
    }

    slen = std::min(view.size, CHECK_DEPTH);
    auto [wmo_offset, wmo_len] = GetWmoOffset(view.data, slen);
    if (wmo_offset >= 0) {
        view.data += (wmo_offset + wmo_len);
        view.size -= (wmo_offset + wmo_len);
    }

    return view;
}

std::vector<uint8_t> FileOpsUtil::DupStrip(const uint8_t* in, size_t len) {
    std::vector<uint8_t> out;
    if (!in || len == 0) return out;
    
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        if (std::iscntrl(in[i]) && in[i] != '\n') continue;
        out.push_back(in[i]);
    }
    return out;
}

std::vector<uint8_t> FileOpsUtil::SerializeMetadata(const ProdInfo& info, uint32_t sz) {
    uint32_t identLen = static_cast<uint32_t>(info.ident.length());
    uint32_t originLen = static_cast<uint32_t>(info.origin.length());
    uint32_t totalLen = 4 + 16 + 4 + 8 + 4 + 4 + 4 + (4 + identLen) + (4 + originLen);

    std::vector<uint8_t> buf;
    buf.reserve(totalLen);

    auto appendData = [&buf](const void* data, size_t len) {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        buf.insert(buf.end(), ptr, ptr + len);
    };

    uint64_t arr_sec = static_cast<uint64_t>(info.arrival.tv_sec);
    int32_t arr_usec = static_cast<int32_t>(info.arrival.tv_usec);
    uint32_t ftype = static_cast<uint32_t>(info.feedtype);
    uint32_t seq = static_cast<uint32_t>(info.seqno);

    appendData(&totalLen, sizeof(totalLen));
    appendData(info.signature.data(), info.signature.size());
    appendData(&sz, sizeof(sz));
    appendData(&arr_sec, sizeof(arr_sec));
    appendData(&arr_usec, sizeof(arr_usec));
    appendData(&ftype, sizeof(ftype));
    appendData(&seq, sizeof(seq));
    appendData(&identLen, sizeof(identLen));
    appendData(info.ident.data(), identLen);
    appendData(&originLen, sizeof(originLen));
    appendData(info.origin.data(), originLen);

    return buf;
}

BufferView FileOpsUtil::PreparePayload(const Product& prod, int entryFlags, std::vector<uint8_t>& scratchpad) {
    BufferView view{prod.data, prod.info.sz};
    
    // 1. Strip WMO headers if requested
    if ((entryFlags & pqact::FL_STRIPWMO) != 0) {
        view = StripHeaders(view.data, view.size);
    }
    
    // 2. Strip non-newline control characters if requested
    if ((entryFlags & pqact::FL_STRIP) != 0) {
        scratchpad = DupStrip(view.data, view.size);
        view.data = scratchpad.data();
        view.size = scratchpad.size();
    }
    
    return view;
}

} // namespace pqact
}
