#include "Engine/Runtime/Threading/Thread.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Platform.h"

#include <exception>
#include <functional>
#include <system_error>
#include <thread>

#if VE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif VE_PLATFORM_APPLE
#include <pthread.h>
#endif

namespace ve
{
    namespace
    {
        thread_local std::string gCurrentThreadName;

        [[nodiscard]] ThreadId MakeThreadId(std::thread::id threadId) noexcept
        {
            if (threadId == std::thread::id{})
            {
                return {};
            }

            const UInt64 hashedId = static_cast<UInt64>(std::hash<std::thread::id>{}(threadId));
            return ThreadId{hashedId == 0 ? 1 : hashedId};
        }

#if VE_PLATFORM_WINDOWS
        [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredLength = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);

            if (requiredLength <= 0)
            {
                return {};
            }

            std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
            MultiByteToWideChar(CP_UTF8,
                                MB_ERR_INVALID_CHARS,
                                text.data(),
                                static_cast<int>(text.size()),
                                wideText.data(),
                                requiredLength);
            return wideText;
        }
#endif

        void ApplyPlatformCurrentThreadName(std::string_view name)
        {
            if (name.empty())
            {
                return;
            }

#if VE_PLATFORM_WINDOWS
            const std::wstring wideName = Utf8ToWide(name);
            if (!wideName.empty())
            {
                static_cast<void>(SetThreadDescription(GetCurrentThread(), wideName.c_str()));
            }
#elif VE_PLATFORM_APPLE
            std::string copiedName(name);
            static_cast<void>(pthread_setname_np(copiedName.c_str()));
#else
            static_cast<void>(name);
#endif
        }
    } // namespace

    struct Thread::State
    {
        std::thread thread;
        ThreadId id;
        std::string name;
    };

    bool operator==(ThreadId left, ThreadId right) noexcept
    {
        return left.value == right.value;
    }

    bool operator!=(ThreadId left, ThreadId right) noexcept
    {
        return !(left == right);
    }

    Thread::Thread()
        : state_(std::make_unique<State>())
    {
    }

    Thread::~Thread()
    {
        if (IsJoinable())
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Thread destroyed while still joinable. Call Join() or Detach() first.");
            std::terminate();
        }
    }

    ErrorCode Thread::StartFunction(const ThreadDesc& desc, ThreadFunction function)
    {
        if (IsJoinable())
        {
            return ErrorCode::InvalidState;
        }

        if (!function)
        {
            return ErrorCode::InvalidArgument;
        }

        state_->name = desc.name;

        try
        {
            std::string threadName = state_->name;
            state_->thread = std::thread(
                [threadName = std::move(threadName), function = std::move(function)]() mutable
                {
                    SetCurrentThreadName(threadName);
                    function();
                });
            state_->id = MakeThreadId(state_->thread.get_id());
        }
        catch (const std::system_error& error)
        {
            (void)error;
            state_->id = {};
            state_->name.clear();
            return ErrorCode::PlatformError;
        }
        catch (const std::bad_alloc&)
        {
            state_->id = {};
            state_->name.clear();
            return ErrorCode::OutOfMemory;
        }

        return ErrorCode::None;
    }

    bool Thread::Join()
    {
        if (!IsJoinable())
        {
            VE_ASSERT_MESSAGE(false, "Thread::Join called on a non-joinable thread.");
            return false;
        }

        if (GetCurrentThreadId() == state_->id)
        {
            VE_ASSERT_MESSAGE(false, "Thread::Join called from the owned thread.");
            return false;
        }

        try
        {
            state_->thread.join();
            state_->id = {};
            return true;
        }
        catch (const std::system_error&)
        {
            VE_ASSERT_MESSAGE(false, "Thread::Join failed unexpectedly.");
            return false;
        }
    }

    bool Thread::Detach()
    {
        if (!IsJoinable())
        {
            VE_ASSERT_MESSAGE(false, "Thread::Detach called on a non-joinable thread.");
            return false;
        }

        try
        {
            state_->thread.detach();
            state_->id = {};
            return true;
        }
        catch (const std::system_error&)
        {
            VE_ASSERT_MESSAGE(false, "Thread::Detach failed unexpectedly.");
            return false;
        }
    }

    bool Thread::IsJoinable() const noexcept
    {
        return state_ != nullptr && state_->thread.joinable();
    }

    ThreadId Thread::GetId() const noexcept
    {
        return state_ == nullptr ? ThreadId{} : state_->id;
    }

    std::string_view Thread::GetName() const noexcept
    {
        return state_ == nullptr ? std::string_view{} : std::string_view(state_->name);
    }

    ThreadId GetCurrentThreadId() noexcept
    {
        return MakeThreadId(std::this_thread::get_id());
    }

    std::string GetCurrentThreadName()
    {
        return gCurrentThreadName;
    }

    void SetCurrentThreadName(std::string_view name)
    {
        gCurrentThreadName.assign(name.begin(), name.end());
        ApplyPlatformCurrentThreadName(gCurrentThreadName);
    }

    void SleepFor(std::chrono::nanoseconds duration)
    {
        std::this_thread::sleep_for(duration);
    }

    void YieldThread() noexcept
    {
        std::this_thread::yield();
    }
} // namespace ve
