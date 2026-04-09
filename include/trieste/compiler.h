#pragma once

/**
 * @file compiler.h
 * @brief Compiler hint macros for Trieste.
 *
 * Provides portable macros for inlining hints and branch prediction.
 * These are self-contained and do not depend on any external library.
 */

#if defined(_MSC_VER) && !defined(__clang__)
#  define TRIESTE_SLOW_PATH __declspec(noinline)
#  define TRIESTE_FAST_PATH __forceinline
#  define TRIESTE_FAST_PATH_INLINE __forceinline
#  define TRIESTE_LIKELY(x) (!!(x))
#  define TRIESTE_UNLIKELY(x) (!!(x))
#  define TRIESTE_USED_FUNCTION
#  if _MSC_VER >= 1927 && _MSVC_LANG > 201703L
#    define TRIESTE_FAST_PATH_LAMBDA [[msvc::forceinline]]
#  else
#    define TRIESTE_FAST_PATH_LAMBDA
#  endif
#else
#  define TRIESTE_SLOW_PATH __attribute__((noinline))
#  define TRIESTE_FAST_PATH __attribute__((always_inline))
#  define TRIESTE_FAST_PATH_INLINE __attribute__((always_inline)) inline
#  define TRIESTE_LIKELY(x) __builtin_expect(!!(x), 1)
#  define TRIESTE_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define TRIESTE_USED_FUNCTION __attribute__((used))
#  define TRIESTE_FAST_PATH_LAMBDA __attribute__((always_inline))
#endif

namespace trieste
{
  template<typename... Args>
  TRIESTE_FAST_PATH_INLINE void UNUSED(Args&&...)
  {}
} // namespace trieste
