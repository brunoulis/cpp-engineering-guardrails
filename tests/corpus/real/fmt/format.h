/*
  Formatting library for C++

  Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

  --- Optional exception to the license ---

  As an exception, if, as a result of your compiling your source code, portions
  of this Software are embedded into a machine-executable object form of such
  source code, you may redistribute such embedded portions in such object form
  without including the above copyright and permission notices.
 */
// Corpus excerpt: intervening original source omitted.
FMT_BEGIN_NAMESPACE
namespace zmij {

enum {
  nonfinite_exp = int(~0u >> 1),
};

// A decimal floating-point number (negative ? -1 : 1) * sig * pow(10, exp).
// If exp is nonfinite_exp then the number is a NaN or an infinity.
template <typename Sig = uint64_t> struct dec_fp {
  Sig sig;  // significand
  int exp;  // exponent
  bool negative;
};

// Floating-point formatting style. Values match std::chars_format, so
// general == fixed | scientific.
enum class format {
  scientific = 1,
  fixed = 2,
  general = 3,
  hex = 4,
};

namespace detail {
struct no_long_double {
  operator double() const noexcept;  // Intentionally undefined.
};
}  // namespace detail

// The long double API's parameter type: long double, or where that API isn't
// provided (IBM double-double), a type no argument converts to, so those
// overloads drop out of overload resolution.
using long_double = std::conditional<FMT_ZMIJ_USE_LONG_DOUBLE, long double,
                                     detail::no_long_double>::type;

// Minimum buffer sizes for the shortest `write`, one per floating-point type.
enum {
  float_buffer_size = 17,
  double_buffer_size = 34,
  // Worst case is IEEE binary128: 1 sign + 36 digits + '.' + "e-dddd".
  long_double_buffer_size = 44,
};

namespace detail {

constexpr auto is_constant_evaluated() noexcept -> bool {
#ifdef __cpp_lib_is_constant_evaluated
  return std::is_constant_evaluated();
#elif FMT_ZMIJ_USE_IS_CONSTANT_EVALUATED
  return __builtin_is_constant_evaluated();
#else
  return false;
#endif
}

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr bool is_big_endian = true;
#else
constexpr bool is_big_endian = false;
#endif

inline auto bswap64(uint64_t x) noexcept -> uint64_t {
#if FMT_ZMIJ_HAS_BUILTIN(__builtin_bswap64) || FMT_ZMIJ_GNUC_BUILTINS
  return __builtin_bswap64(x);
#elif FMT_ZMIJ_MSC_VER
  return _byteswap_uint64(x);
#else
  return ((x & 0xff00000000000000) >> 56) | ((x & 0x00ff000000000000) >> 40) |
         ((x & 0x0000ff0000000000) >> 24) | ((x & 0x000000ff00000000) >> +8) |
         ((x & 0x00000000ff000000) << +8) | ((x & 0x0000000000ff0000) << 24) |
         ((x & 0x000000000000ff00) << 40) | ((x & 0x00000000000000ff) << 56);
#endif
}

inline FMT_ZMIJ_CONSTEXPR20 auto clz(uint64_t x) noexcept -> int {
  assert(x != 0);
  if (!is_constant_evaluated()) {
#if FMT_ZMIJ_HAS_BUILTIN(__builtin_clzll) || FMT_ZMIJ_GNUC_BUILTINS
    return __builtin_clzll(x);
#elif defined(_M_AMD64) && defined(__AVX2__)
    // Use lzcnt only on AVX2-capable CPUs that have this BMI instruction.
    return __lzcnt64(x);
#elif defined(_M_AMD64) || defined(_M_ARM64)
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return 63 - idx;
#elif FMT_ZMIJ_MSC_VER
    unsigned long idx;
    if (_BitScanReverse(&idx, uint32_t(x >> 32))) return 31 - idx;
    _BitScanReverse(&idx, uint32_t(x));
    return 63 - idx;
#endif
  }
  int n = 64;
  for (; x > 0; x >>= 1) --n;
  return n;
}
