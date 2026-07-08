#pragma once
#include <rpc/rpc.h>
#include "FeedType.h"
#include "ProdClass.h"

#ifdef NULLPROC
#undef NULLPROC
#endif

namespace rdm {

// Should be in a namespace I think.
constexpr unsigned long LDM_PROG = 300029;
constexpr unsigned long FIVE     = 5;
constexpr unsigned long SIX      = 6;

constexpr unsigned long NULLPROC     = 0;
constexpr unsigned long HEREIS       = 1;
constexpr unsigned long FEEDME       = 4;
constexpr unsigned long HIYA         = 5;
constexpr unsigned long NOTIFICATION = 8;
constexpr unsigned long NOTIFYME     = 9;
constexpr unsigned long COMINGSOON   = 12;
constexpr unsigned long BLKDATA      = 13;
constexpr unsigned long IS_ALIVE     = 14;

constexpr unsigned int DATAPKT_RPC_OVERHEAD = 72;
constexpr unsigned int MAX_RPC_BUF_NEEDED   = DATAPKT_RPC_OVERHEAD + 262144;

inline std::string GetRpcProcName(unsigned long proc) {
    switch (proc) {
        case NULLPROC:     return "NULLPROC";
        case HEREIS:       return "HEREIS";
        case FEEDME:       return "FEEDME";
        case HIYA:         return "HIYA";
        case NOTIFICATION: return "NOTIFICATION";
        case NOTIFYME:     return "NOTIFYME";
        case COMINGSOON:   return "COMINGSOON";
        case BLKDATA:      return "BLKDATA";
        case IS_ALIVE:     return "IS_ALIVE";
        default:           return std::to_string(proc);
    }
}

struct MutablePayload {
    uint8_t* buffer;
    size_t capacity;
    size_t bytes_written;
};

struct MutableProduct {
    ProdInfo info;
    MutablePayload payload;
};

struct FeedParNet {
    ProdClass clss;
    u_int max_hereis;
};

struct ComingSoonArgsNet {
    ProdInfo info;
    u_int pktsz;
};

struct DataPktNet {
    uint8_t signaturep[16];
    u_int pktnum;
    char* dbuf_val;
    u_int dbuf_len;
};

struct ServiceAddrNet {
    char* inetId;
    u_short port;
};

}
