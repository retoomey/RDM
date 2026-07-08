#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <optional>
#include <cstring>
#include <type_traits>

namespace rdm {
class Signature {
private:
  std::array<uint8_t, 16> bytes_;

public:
  uint8_t * data(){ return bytes_.data(); }

  const uint8_t *
  data() const { return bytes_.data(); }

  constexpr size_t
  size() const { return bytes_.size(); }

  void fill(uint8_t val){ bytes_.fill(val); }

  uint8_t&
  operator [] (size_t pos){ return bytes_[pos]; }

  const uint8_t&
  operator [] (size_t pos) const { return bytes_[pos]; }

  bool
  operator == (const Signature& rhs) const { return bytes_ == rhs.bytes_; }

  bool
  operator != (const Signature& rhs) const { return bytes_ != rhs.bytes_; }

  std::string
  ToString() const;
  static std::optional<Signature>
  Parse(const std::string& hexStr);

  static Signature
  FromRaw(const uint8_t * raw_bytes)
  {
    Signature sig;

    std::memcpy(sig.bytes_.data(), raw_bytes, sig.bytes_.size());
    return sig;
  }

  void
  CopyTo(uint8_t * dest) const
  {
    std::memcpy(dest, bytes_.data(), bytes_.size());
  }

  bool
  Equals(const uint8_t * raw_bytes) const
  {
    return std::memcmp(bytes_.data(), raw_bytes, bytes_.size()) == 0;
  }

  // One-shot helper for contiguous data blocks
  static Signature
  GenerateMD5(const void * data, size_t size);

  // Incremental helper for chained/composite hashing
  class Hasher {
public:
    Hasher();
    void
    Update(const void * data, size_t size);
    Signature
    Finalize();
  };
};

// Guardrail to ensure this remains completely zero-copy compatible with the mmap layer
static_assert(std::is_trivial_v<Signature> && std::is_standard_layout_v<Signature>,
  "Signature must remain a POD type for memory-mapped queue and RPC compatibility");
}
