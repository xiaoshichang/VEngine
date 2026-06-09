#pragma once

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <atomic>

namespace ve
{
#if VE_BUILD_DEBUG
    namespace detail
    {
        inline std::atomic<UInt64> gExpectedSceneThreadIdValue{0};
        inline std::atomic<UInt64> gExpectedRenderThreadIdValue{0};
    } // namespace detail

    inline void SetExpectedSceneThreadId(ThreadId threadId) noexcept
    {
        detail::gExpectedSceneThreadIdValue.store(threadId.value, std::memory_order_release);
    }

    [[nodiscard]] inline ThreadId GetExpectedSceneThreadId() noexcept
    {
        return ThreadId{detail::gExpectedSceneThreadIdValue.load(std::memory_order_acquire)};
    }

    inline void SetExpectedRenderThreadId(ThreadId threadId) noexcept
    {
        detail::gExpectedRenderThreadIdValue.store(threadId.value, std::memory_order_release);
    }

    [[nodiscard]] inline ThreadId GetExpectedRenderThreadId() noexcept
    {
        return ThreadId{detail::gExpectedRenderThreadIdValue.load(std::memory_order_acquire)};
    }
#else
    inline void SetExpectedSceneThreadId(ThreadId threadId) noexcept
    {
        (void)threadId;
    }

    [[nodiscard]] inline ThreadId GetExpectedSceneThreadId() noexcept
    {
        return ThreadId{};
    }

    inline void SetExpectedRenderThreadId(ThreadId threadId) noexcept
    {
        (void)threadId;
    }

    [[nodiscard]] inline ThreadId GetExpectedRenderThreadId() noexcept
    {
        return ThreadId{};
    }
#endif
} // namespace ve

#if VE_BUILD_DEBUG
#define VE_ASSERT_SCENE_THREAD()                                                                                      \
    do                                                                                                                \
    {                                                                                                                 \
        const ::ve::ThreadId veExpectedSceneThreadId = ::ve::GetExpectedSceneThreadId();                            \
        VE_ASSERT_MESSAGE(veExpectedSceneThreadId.IsValid(), "Expected Scene Thread id is not registered.");         \
        VE_ASSERT_MESSAGE(::ve::GetCurrentThreadId() == veExpectedSceneThreadId,                                     \
                          "This logic must run on the Scene Thread.");                                                \
    } while (false)

#define VE_ASSERT_RENDER_THREAD()                                                                                     \
    do                                                                                                                \
    {                                                                                                                 \
        const ::ve::ThreadId veExpectedRenderThreadId = ::ve::GetExpectedRenderThreadId();                          \
        VE_ASSERT_MESSAGE(veExpectedRenderThreadId.IsValid(), "Expected Render Thread id is not registered.");       \
        VE_ASSERT_MESSAGE(::ve::GetCurrentThreadId() == veExpectedRenderThreadId,                                    \
                          "This logic must run on the Render Thread.");                                               \
    } while (false)
#else
#define VE_ENSURE_SCENE_THREAD()                                                                                      \
    do                                                                                                                \
    {                                                                                                                 \
    } while (false)

#define VE_ENSURE_RENDER_THREAD()                                                                                     \
    do                                                                                                                \
    {                                                                                                                 \
    } while (false)
#endif
