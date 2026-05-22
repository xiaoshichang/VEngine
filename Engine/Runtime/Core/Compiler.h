#pragma once

#if !defined(VE_COMPILER_CLANG)
#if defined(__clang__)
#define VE_COMPILER_CLANG 1
#else
#define VE_COMPILER_CLANG 0
#endif
#endif

#if !defined(VE_COMPILER_MSVC)
#if defined(_MSC_VER) && !VE_COMPILER_CLANG
#define VE_COMPILER_MSVC 1
#else
#define VE_COMPILER_MSVC 0
#endif
#endif

#if !defined(VE_COMPILER_GCC)
#if defined(__GNUC__) && !VE_COMPILER_CLANG
#define VE_COMPILER_GCC 1
#else
#define VE_COMPILER_GCC 0
#endif
#endif

#if VE_COMPILER_MSVC
#define VE_FORCE_INLINE __forceinline
#define VE_NOINLINE __declspec(noinline)
#elif VE_COMPILER_CLANG || VE_COMPILER_GCC
#define VE_FORCE_INLINE inline __attribute__((always_inline))
#define VE_NOINLINE __attribute__((noinline))
#else
#define VE_FORCE_INLINE inline
#define VE_NOINLINE
#endif
