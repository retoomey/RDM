#include "SunRpcXdr.h"
#include "Log.h"
#include "Registry.h"
#include <cstdlib>

namespace rdm {

// NEW: Safely handles decoding and memory allocation bounds-checking.
bool_t xdr_net_mutable_product(XDR* xdrs, MutableProduct* mprod) {
    if (!xdr_net_prod_info(xdrs, &mprod->info)) return FALSE;
    u_int data_sz = mprod->info.sz;

    if (xdrs->x_op == XDR_FREE) {
        // Only free if we fell back to malloc (indicated by 0 capacity)
        if (mprod->payload.buffer && mprod->payload.capacity == 0) {
            free(mprod->payload.buffer);
            mprod->payload.buffer = nullptr;
        }
        return TRUE;
    }

    if (xdrs->x_op == XDR_DECODE) {
        if (data_sz > 0) {
            // Bounds check against explicit capacity
            if (mprod->payload.buffer != nullptr && data_sz <= mprod->payload.capacity) {
                mprod->payload.bytes_written = data_sz;
            } else {
                // Making sure malicious actor can't overflow RAM (security patch)
                unsigned int max_allowed_bytes = registry::getMaxProductSizeBytes();
                
                if (data_sz > max_allowed_bytes) {
                    LogError("Rejecting oversized product: {} bytes exceeds configured maximum of {} bytes", data_sz, max_allowed_bytes);
                    return FALSE; // Aborts XDR decode and safely drops the connection
                }

                // Fallback to malloc for payloads within the acceptable registry limit
                mprod->payload.buffer = reinterpret_cast<uint8_t*>(malloc(data_sz));
                mprod->payload.capacity = 0; // 0 flags that this was dynamically allocated
                mprod->payload.bytes_written = data_sz;
                if (!mprod->payload.buffer) return FALSE;
            }
            if (!xdr_opaque(xdrs, reinterpret_cast<char*>(mprod->payload.buffer), data_sz)) {
                if (mprod->payload.capacity == 0) free(mprod->payload.buffer);
                return FALSE;
            }
        } else {
            mprod->payload.buffer = nullptr;
            mprod->payload.bytes_written = 0;
        }
    }
    return TRUE;
}

bool_t xdr_net_prod_info(XDR* xdrs, ProdInfo* info) {
    if (xdrs->x_op == XDR_FREE) return TRUE;

    long tv_sec = info->arrival.tv_sec;
    long tv_usec = info->arrival.tv_usec;
    if (!xdr_long(xdrs, &tv_sec)) return FALSE;
    if (!xdr_long(xdrs, &tv_usec)) return FALSE;
    
    if (!xdr_opaque(xdrs, reinterpret_cast<char*>(info->signature.data()), 16)) return FALSE;

    char* origin_ptr = (xdrs->x_op == XDR_ENCODE) ? const_cast<char*>(info->origin.c_str()) : nullptr;
    if (!xdr_string(xdrs, &origin_ptr, 64)) return FALSE;

    u_int feedtype = info->feedtype.GetValue();
    if (!xdr_u_int(xdrs, &feedtype)) return FALSE;

    u_int seqno = info->seqno;
    if (!xdr_u_int(xdrs, &seqno)) return FALSE;

    char* ident_ptr = (xdrs->x_op == XDR_ENCODE) ? const_cast<char*>(info->ident.c_str()) : nullptr;
    if (!xdr_string(xdrs, &ident_ptr, 255)) return FALSE;

    u_int sz = info->sz;
    if (!xdr_u_int(xdrs, &sz)) return FALSE;

    if (xdrs->x_op == XDR_DECODE) {
        info->arrival.tv_sec = tv_sec;
        info->arrival.tv_usec = tv_usec;
        info->feedtype = FeedType(feedtype);
        info->seqno = seqno;
        info->sz = sz;
        
        info->origin = origin_ptr ? origin_ptr : "";
        if (origin_ptr) free(origin_ptr);

        info->ident = ident_ptr ? ident_ptr : "";
        if (ident_ptr) free(ident_ptr);
    }
    
    return TRUE;
}

bool_t xdr_net_product(XDR* xdrs, Product* prod) {
    if (!xdr_net_prod_info(xdrs, &prod->info)) return FALSE;
    u_int data_sz = prod->info.sz;
    
    // --- DEPRECATED: XDR_FREE and XDR_DECODE blocks ---
    // Deprecated because Product.data is a const pointer. Forcing a decode 
    // violates const correctness and obscures capacity tracking.
    
    if (xdrs->x_op == XDR_ENCODE) {
        if (data_sz > 0) {
            char* data_ptr = const_cast<char*>(reinterpret_cast<const char*>(prod->data));
            if (!xdr_opaque(xdrs, data_ptr, data_sz)) return FALSE;
        }
        return TRUE;
    }
    
    // Failsafe: Should never be reached for DECODE or FREE operations
    return FALSE;
}

bool_t xdr_net_prod_spec(XDR* xdrs, ProdSpec* spec) {
    if (xdrs->x_op == XDR_FREE) return TRUE;

    u_int ft = spec->feedtype;
    if (!xdr_u_int(xdrs, &ft)) return FALSE;
    if (xdrs->x_op == XDR_DECODE) spec->feedtype = ft;

    char* pat_ptr = (xdrs->x_op == XDR_ENCODE) ? const_cast<char*>(spec->pattern.c_str()) : nullptr;
    if (!xdr_string(xdrs, &pat_ptr, 255)) return FALSE;
    if (xdrs->x_op == XDR_DECODE) {
        spec->pattern = pat_ptr ? pat_ptr : "";
        if (pat_ptr) free(pat_ptr);
    }

    return TRUE;
}

bool_t xdr_net_prod_class(XDR* xdrs, ProdClass* clss) {
    if (xdrs->x_op == XDR_FREE) return TRUE;

    long f_sec = clss->from_sec, f_usec = clss->from_usec;
    long t_sec = clss->to_sec, t_usec = clss->to_usec;

    if (!xdr_long(xdrs, &f_sec)) return FALSE;
    if (!xdr_long(xdrs, &f_usec)) return FALSE;
    if (!xdr_long(xdrs, &t_sec)) return FALSE;
    if (!xdr_long(xdrs, &t_usec)) return FALSE;

    if (xdrs->x_op == XDR_DECODE) {
        clss->from_sec = f_sec; clss->from_usec = f_usec;
        clss->to_sec = t_sec; clss->to_usec = t_usec;
    }

    u_int psa_len = clss->specs.size();
    if (!xdr_u_int(xdrs, &psa_len)) return FALSE;

    if (xdrs->x_op == XDR_DECODE) clss->specs.resize(psa_len);
    for (u_int i = 0; i < psa_len; ++i) {
        if (!xdr_net_prod_spec(xdrs, &clss->specs[i])) return FALSE;
    }

    return TRUE;
}

bool_t xdr_net_feedpar(XDR* xdrs, FeedParNet* fpar) {
    if (xdrs->x_op == XDR_FREE) return TRUE;

    bool_t is_present = TRUE;
    if (!xdr_bool(xdrs, &is_present)) return FALSE;

    if (is_present) {
        if (!xdr_net_prod_class(xdrs, &fpar->clss)) return FALSE;
    }

    if (!xdr_u_int(xdrs, &fpar->max_hereis)) return FALSE;

    return TRUE;
}

bool_t xdr_net_fornme_reply(XDR* xdrs, FeedResponse* reply) {
    if (xdrs->x_op == XDR_FREE) return TRUE;

    int code;
    if (xdrs->x_op == XDR_ENCODE) {
        code = static_cast<int>(reply->statusCode);
    }
    if (!xdr_int(xdrs, &code)) return FALSE;
    if (xdrs->x_op == XDR_DECODE) {
        reply->statusCode = static_cast<ReplyStatus>(code);
    }

    if (code == 0) {
        if (!xdr_u_int(xdrs, &reply->feedProcessId)) return FALSE;
    } else if (code == 7) {
        bool_t is_present = TRUE;
        if (!xdr_bool(xdrs, &is_present)) return FALSE;
        if (is_present) {
            if (!xdr_net_prod_class(xdrs, &reply->allowedClass)) return FALSE;
        }
    }

    return TRUE;
}

bool_t xdr_net_hiya_reply(XDR* xdrs, HiyaResponse* reply) {
    if (xdrs->x_op == XDR_FREE) return TRUE;

    int code;
    if (xdrs->x_op == XDR_ENCODE) {
        code = static_cast<int>(reply->statusCode);
    }
    if (!xdr_int(xdrs, &code)) return FALSE;
    if (xdrs->x_op == XDR_DECODE) {
        reply->statusCode = static_cast<ReplyStatus>(code);
    }

    if (code == 0) {
        if (!xdr_u_int(xdrs, &reply->maxHereis)) return FALSE;
    } else if (code == 7) {
        FeedParNet fpar;
        if (xdrs->x_op == XDR_ENCODE) {
            fpar.clss = reply->acceptedClass;
            fpar.max_hereis = reply->maxHereis;
        }
        if (!xdr_net_feedpar(xdrs, &fpar)) return FALSE;
        if (xdrs->x_op == XDR_DECODE) {
            reply->acceptedClass = fpar.clss;
            reply->maxHereis = fpar.max_hereis;
        }
    }

    return TRUE;
}

bool_t xdr_net_comingsoon_args(XDR* xdrs, ComingSoonArgsNet* args) {
    if (xdrs->x_op == XDR_FREE) return TRUE;

    bool_t is_present = TRUE;
    if (!xdr_bool(xdrs, &is_present)) return FALSE;

    if (is_present) {
        if (!xdr_net_prod_info(xdrs, &args->info)) return FALSE;
    }

    if (!xdr_u_int(xdrs, &args->pktsz)) return FALSE;

    return TRUE;
}

bool_t xdr_net_datapkt(XDR* xdrs, DataPktNet* pkt) {
    if (xdrs->x_op == XDR_FREE) {
        if (pkt->dbuf_val) {
            free(pkt->dbuf_val);
            pkt->dbuf_val = nullptr;
        }
        return TRUE;
    }

    bool_t is_present = TRUE;
    if (!xdr_bool(xdrs, &is_present)) return FALSE;

    if (is_present) {
        if (!xdr_opaque(xdrs, reinterpret_cast<char*>(pkt->signaturep), 16)) return FALSE;
    }

    if (!xdr_u_int(xdrs, &pkt->pktnum)) return FALSE;
    if (!xdr_bytes(xdrs, &pkt->dbuf_val, &pkt->dbuf_len, ~0u)) return FALSE;

    return TRUE;
}

}
