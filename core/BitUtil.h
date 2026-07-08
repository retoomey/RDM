#pragma once
#include <type_traits>

namespace rdm {
template <typename T, typename F>
constexpr void
fSet(T& t, F f)
{
  static_assert(std::is_integral_v<T> && std::is_integral_v<F>, "Bitwise operations require integral types");
  t |= static_cast<T>(f);
}

template <typename T, typename F>
constexpr void
fClr(T& t, F f)
{
  static_assert(std::is_integral_v<T> && std::is_integral_v<F>, "Bitwise operations require integral types");
  t &= ~static_cast<T>(f);
}

template <typename T, typename F>
constexpr bool
fIsSet(T t, F f)
{
  static_assert(std::is_integral_v<T> && std::is_integral_v<F>, "Bitwise operations require integral types");
  return (t & static_cast<T>(f)) != 0;
}

template <typename T, typename F>
constexpr T
fMask(T t, F f)
{
  static_assert(std::is_integral_v<T> && std::is_integral_v<F>, "Bitwise operations require integral types");
  return t & ~static_cast<T>(f);
}
}
