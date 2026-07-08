#include "Signature.h"
#include "Log.h"
#include <cstdio>
#include <memory>
#include <openssl/evp.h>

namespace rdm {

std::string Signature::ToString() const {
    char buf[33];
    char* bp = buf;
    for (uint8_t byte : bytes_) {
        bp += std::snprintf(bp, sizeof(buf) - (bp - buf), "%02x", byte);
    }
    return std::string(buf);
}

std::optional<Signature> Signature::Parse(const std::string& hexStr) {
    if (hexStr.length() < 32) return std::nullopt;
    Signature tmpSig;
    for (size_t i = 0; i < 16; i++) {
        unsigned value;
        if (std::sscanf(hexStr.c_str() + 2 * i, "%2x", &value) != 1) {
            LogSyserr("Couldn't parse signature \"{}\"", hexStr);
            return std::nullopt;
        }
        tmpSig[i] = static_cast<uint8_t>(value);
    }
    return tmpSig;
}

// ==============================================================================
// Optimized MD5 Hashing Implementation
// ==============================================================================

// Global thread-local cache. Allocates ONCE per thread lifecycle.
// This entirely eliminates malloc/free thrashing during high-speed ingestion.
static thread_local std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> 
    tl_mdctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);

// --- Incremental Hasher Implementation ---

Signature::Hasher::Hasher() {
    if (tl_mdctx) {
        // Resets the state of the existing context
        EVP_DigestInit_ex(tl_mdctx.get(), EVP_md5(), nullptr);
    } else {
        LogError("Failed to allocate OpenSSL EVP context");
    }
}

void Signature::Hasher::Update(const void* data, size_t size) {
    if (tl_mdctx && size > 0 && data != nullptr) {
        EVP_DigestUpdate(tl_mdctx.get(), data, size);
    }
}

Signature Signature::Hasher::Finalize() {
    Signature sig;
    sig.fill(0);
    if (tl_mdctx) {
        unsigned int md_len = 0;
        EVP_DigestFinal_ex(tl_mdctx.get(), sig.data(), &md_len);
    }
    return sig;
}

// --- One-Shot Helper ---

Signature Signature::GenerateMD5(const void* data, size_t size) {
    Hasher hasher;
    hasher.Update(data, size);
    return hasher.Finalize();
}

}
