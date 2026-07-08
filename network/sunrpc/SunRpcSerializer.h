#pragma once
#include "IProductSerializer.h"

namespace rdm {
/**
 * @brief Sun XDR implementation of the disk-based product queue serializer.
 * * @warning STRICTLY FOR DISK I/O. Do NOT use this for network serialization. 
 * The network layer uses standard `xdr_bytes` which injects a 4-byte length 
 * prefix and pads data to 4-byte boundaries. This class intentionally omits 
 * both to match legacy LDM disk storage optimizations.
 */
class SunRpcSerializer : public IProductSerializer {
public:
    SunRpcSerializer() = default;
    ~SunRpcSerializer() override = default;

    size_t GetEncodedSize(const Product& prod) const override;
    size_t GetEncodedInfoSize(const ProdInfo& info) const override;
    bool EncodeProduct(void* buffer, size_t buffer_size, const Product& prod) const override;
    bool DecodeProdInfo(const void* buffer, size_t buffer_size, ProdInfo& info, void** next_ptr = nullptr) const override;
    void* EncodeProdInfo(void* buffer, size_t buffer_size, const ProdInfo& info) const override;
    bool UpdateSignature(void* buffer, size_t buffer_size, const Signature& new_sig) const override;
};

}
