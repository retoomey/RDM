#pragma once

#include <cstdint>
#include <string>
#include <optional>
#include <chrono>

#include <sys/time.h>

namespace rdm {
class Timestamp {
public:
  int64_t tv_sec{ 0 };
  int32_t tv_usec{ 0 };

  Timestamp() = default;
  Timestamp(int64_t sec, int32_t usec) : tv_sec(sec), tv_usec(usec){ }

  static const Timestamp NONE;
  static const Timestamp ZERO;
  static const Timestamp ENDT;

  static Timestamp
  Now();
  static Timestamp
  FromTimeval(const struct timeval & tv);

  struct timeval ToTimeval () const;

  Timestamp
  operator + (const Timestamp& rhs) const;
  Timestamp
  operator - (const Timestamp& rhs) const;
  Timestamp&
  operator += (const Timestamp& rhs);
  Timestamp&
  operator -= (const Timestamp& rhs);

  bool
  operator == (const Timestamp& rhs) const;
  bool
  operator != (const Timestamp& rhs) const;
  bool
  operator < (const Timestamp& rhs) const;
  bool
  operator <= (const Timestamp& rhs) const;
  bool
  operator > (const Timestamp& rhs) const;
  bool
  operator >= (const Timestamp& rhs) const;

  double
  AsSeconds() const;

  void
  IncrementMicrosecond();
  void
  DecrementMicrosecond();

  std::string
  ToString() const;
  static std::optional<Timestamp>
  Parse(const std::string& str);
};

// Guarantee layout compatibility with legacy C structs at compile time
static_assert(sizeof(Timestamp) == sizeof(struct timeval), "Timestamp must match struct timeval layout!");
}
