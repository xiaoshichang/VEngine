#pragma once

#include "Engine/Runtime/Core/Compiler.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if VE_COMPILER_MSVC
#include <intrin.h>
#endif

#if !defined(VE_PLATFORM_WINDOWS)
#if defined(_WIN32)
#define VE_PLATFORM_WINDOWS 1
#else
#define VE_PLATFORM_WINDOWS 0
#endif
#endif

#if !defined(VE_PLATFORM_APPLE)
#if defined(__APPLE__)
#define VE_PLATFORM_APPLE 1
#else
#define VE_PLATFORM_APPLE 0
#endif
#endif

#if !defined(VE_PLATFORM_MACOS)
#if defined(__APPLE__) && defined(TARGET_OS_OSX) && TARGET_OS_OSX
#define VE_PLATFORM_MACOS 1
#else
#define VE_PLATFORM_MACOS 0
#endif
#endif

#if !defined(VE_PLATFORM_IOS)
#if defined(__APPLE__) && defined(TARGET_OS_IOS) && TARGET_OS_IOS
#define VE_PLATFORM_IOS 1
#else
#define VE_PLATFORM_IOS 0
#endif
#endif

#if !defined(VE_DEBUG_BREAK)
#if VE_COMPILER_MSVC
#define VE_DEBUG_BREAK() __debugbreak()
#elif VE_COMPILER_CLANG
#define VE_DEBUG_BREAK() __builtin_debugtrap()
#elif VE_COMPILER_GCC
#define VE_DEBUG_BREAK() __builtin_trap()
#else
#define VE_DEBUG_BREAK() ((void)0)
#endif
#endif
