#include "SunRpcSerializer.h"
#include "SunRpcXdr.h"
#include <rpc/xdr.h>
#include <cstring>
#include <cstdlib>

namespace rdm {

namespace {
    constexpr size_t XDR_UNIT = 4;
    
    inline size_t rndup(size_t len) {
        return ((len + XDR_UNIT - 1) / XDR_UNIT) * XDR_UNIT;
    }
    
    inline size_t xlen_string(size_t len) {
        return XDR_UNIT + rndup(len);
    }
}

size_t SunRpcSerializer::GetEncodedInfoSize(const ProdInfo& info) const {
    size_t len = 0;
    len += 8;                                 // tv_sec (4) + tv_usec (4)
    len += 16;                                // opaque signature[16]
    len += xlen_string(info.origin.length()); // origin string
    len += XDR_UNIT;                          // feedtype (u_int)
    len += XDR_UNIT;                          // seqno (u_int)
    len += xlen_string(info.ident.length());  // ident string
    len += XDR_UNIT;                          // sz (u_int)
    return len;
}

size_t SunRpcSerializer::GetEncodedSize(const Product& prod) const {
    // ------------------------------------------------------------------------
    // DISK OPTIMIZATION: 
    // Do NOT add XDR_UNIT (4-byte length prefix) or XDR padding (rndup).
    // The data is packed as tight as possible immediately following the header.
    // ------------------------------------------------------------------------
    return GetEncodedInfoSize(prod.info) + prod.info.sz;
}

bool SunRpcSerializer::EncodeProduct(void* buffer, size_t buffer_size, const Product& prod) const {
    XDR xdrs;
    xdrmem_create(&xdrs, static_cast<char*>(buffer), buffer_size, XDR_ENCODE);

    if (!xdr_net_prod_info(&xdrs, const_cast<ProdInfo*>(&prod.info))) {
        xdr_destroy(&xdrs);
        return false;
    }
    
    u_int pos = xdr_getpos(&xdrs);
    std::memcpy(static_cast<char*>(buffer) + pos, prod.data, prod.info.sz);
    xdr_destroy(&xdrs);
    return true;
}

void* SunRpcSerializer::EncodeProdInfo(void* buffer, size_t buffer_size, const ProdInfo& info) const {
    XDR xdrs;
    xdrmem_create(&xdrs, static_cast<char*>(buffer), buffer_size, XDR_ENCODE);

    if (!xdr_net_prod_info(&xdrs, const_cast<ProdInfo*>(&info))) {
        xdr_destroy(&xdrs);
        return nullptr;
    }
    
    void* data_ptr = static_cast<char*>(buffer) + xdr_getpos(&xdrs);
    xdr_destroy(&xdrs);
    return data_ptr;
}

bool SunRpcSerializer::DecodeProdInfo(const void* buffer, size_t buffer_size, ProdInfo& info, void** next_ptr) const {
    XDR xdrs;
    xdrmem_create(&xdrs, static_cast<char*>(const_cast<void*>(buffer)), buffer_size, XDR_DECODE);

    bool success = xdr_net_prod_info(&xdrs, &info);
    
    if (success && next_ptr) {
        *next_ptr = static_cast<char*>(const_cast<void*>(buffer)) + xdr_getpos(&xdrs);
    }
    
    xdr_destroy(&xdrs);
    return success;
}

bool SunRpcSerializer::UpdateSignature(void* buffer, size_t buffer_size, const Signature& new_sig) const {
    // Modernized: use new_sig.size() instead of sizeof(signaturet)
    if (!buffer || buffer_size < (8 + new_sig.size())) return false;
    char* xp = static_cast<char*>(buffer);
    xp += 8;
    
    // Modernized: Use .data() to access the raw bytes
    std::memcpy(xp, new_sig.data(), new_sig.size());
    
    return true;
}

}
