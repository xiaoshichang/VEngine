#pragma once

#include "Engine/Runtime/Core/BuildConfig.h"
#include "Engine/Runtime/Core/Platform.h"

namespace ve
{
/// Describes a single assertion failure with source context captured at the call site.
struct AssertionInfo
{
    const char* expression = nullptr;
    const char* message = nullptr;
    const char* file = nullptr;
    const char* function = nullptr;
    int line = 0;
};

/// Receives assertion failures before the assertion macro applies its local break policy.
using AssertionHandler = void (*)(const AssertionInfo& info);

/// Replaces the process-wide assertion handler. Passing nullptr restores the default handler.
void SetAssertionHandler(AssertionHandler handler) noexcept;

/// Returns the currently active assertion handler.
[[nodiscard]] AssertionHandler GetAssertionHandler() noexcept;

/// Reports an assertion failure to the active handler without triggering a debug break.
void ReportAssertionFailure(const AssertionInfo& info) noexcept;
}

#define VE_DETAIL_REPORT_ASSERTION_FAILURE(expressionText, messageText)                                              \
    do                                                                                                              \
    {                                                                                                               \
        const ::ve::AssertionInfo assertionInfo{expressionText, messageText, __FILE__, __func__, __LINE__};          \
        ::ve::ReportAssertionFailure(assertionInfo);                                                                \
    } while (false)

#if VE_BUILD_DEBUG
#define VE_DETAIL_DEBUG_BREAK_AFTER_ASSERTION() VE_DEBUG_BREAK()
#else
#define VE_DETAIL_DEBUG_BREAK_AFTER_ASSERTION() ((void)0)
#endif

#if VE_BUILD_DEBUG
#define VE_ASSERT_MESSAGE(expression, message)                                                                      \
    do                                                                                                              \
    {                                                                                                               \
        if (!(expression))                                                                                          \
        {                                                                                                           \
            VE_DETAIL_REPORT_ASSERTION_FAILURE(#expression, message);                                               \
            VE_DETAIL_DEBUG_BREAK_AFTER_ASSERTION();                                                                \
        }                                                                                                           \
    } while (false)
#else
#define VE_ASSERT_MESSAGE(expression, message)                                                                      \
    do                                                                                                              \
    {                                                                                                               \
        (void)sizeof(expression);                                                                                   \
        (void)sizeof(message);                                                                                      \
    } while (false)
#endif

#define VE_ASSERT(expression) VE_ASSERT_MESSAGE(expression, nullptr)

#if VE_BUILD_DEBUG
#define VE_VERIFY_MESSAGE(expression, message) VE_ASSERT_MESSAGE(expression, message)
#else
#define VE_VERIFY_MESSAGE(expression, message)                                                                      \
    do                                                                                                              \
    {                                                                                                               \
        (void)(expression);                                                                                         \
        (void)sizeof(message);                                                                                      \
    } while (false)
#endif

#define VE_VERIFY(expression) VE_VERIFY_MESSAGE(expression, nullptr)

#define VE_ASSERT_ALWAYS_MESSAGE(expression, message)                                                               \
    do                                                                                                              \
    {                                                                                                               \
        if (!(expression))                                                                                          \
        {                                                                                                           \
            VE_DETAIL_REPORT_ASSERTION_FAILURE(#expression, message);                                               \
            VE_DETAIL_DEBUG_BREAK_AFTER_ASSERTION();                                                                \
        }                                                                                                           \
    } while (false)

#define VE_ASSERT_ALWAYS(expression) VE_ASSERT_ALWAYS_MESSAGE(expression, nullptr)

#define VE_UNREACHABLE() VE_ASSERT_ALWAYS_MESSAGE(false, "Unreachable code reached")
