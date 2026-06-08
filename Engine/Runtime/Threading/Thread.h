#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <chrono>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace ve
{
    /// Opaque VEngine thread identifier.
    ///
    /// ThreadId is intentionally not a platform handle and is not suitable for native API calls. It exists so engine
    /// code can compare and report thread identities without depending on std::thread::id, HANDLE, or pthread_t in
    /// public APIs.
    struct ThreadId
    {
        /// Process-local diagnostic id value. A value of zero means "no thread"; ids may be reused after a thread
        /// exits.
        UInt64 value = 0;

        /// Returns true when this id refers to a known thread.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return value != 0;
        }
    };

    [[nodiscard]] bool operator==(ThreadId left, ThreadId right) noexcept;
    [[nodiscard]] bool operator!=(ThreadId left, ThreadId right) noexcept;

    /// Describes a thread before it is started.
    ///
    /// The first-stage descriptor only stores a diagnostic name. Future priority and affinity fields should be added
    /// here without exposing platform handles or changing normal Thread call sites.
    struct ThreadDesc
    {
        /// Optional diagnostic name copied by Thread::Start(). Empty names are allowed.
        std::string name;
    };

    /// Owns one cross-platform thread.
    ///
    /// Thread is non-copyable and non-movable so ownership and shutdown order remain explicit. A Thread object owns at
    /// most one joinable thread at a time. Owners must call Join() or Detach() before destruction.
    class Thread : public NonMovable
    {
    public:
        Thread();
        ~Thread();

        /// Starts a new thread with a copied descriptor and callable entry point.
        ///
        /// Returns InvalidState when this Thread already owns a joinable thread. Returns InvalidArgument when the
        /// callable cannot be stored as a valid entry point. Returns PlatformError when the platform thread cannot be
        /// created.
        template<typename Callable>
        [[nodiscard]] ErrorCode Start(const ThreadDesc& desc, Callable&& callable)
        {
            try
            {
                std::function<void()> function(std::forward<Callable>(callable));
                return StartFunction(desc, std::move(function));
            }
            catch (const std::bad_alloc&)
            {
                return ErrorCode::OutOfMemory;
            }
        }

        /// Joins the owned thread.
        ///
        /// Returns true when a joinable thread was joined. Returns false when this Thread is not joinable or when
        /// joining would be invalid, such as joining from the same thread. Invalid joins are API misuse and assert in
        /// debug builds.
        [[nodiscard]] bool Join();

        /// Detaches the owned thread.
        ///
        /// Returns true when a joinable thread was detached. Returns false when this Thread is not joinable. Invalid
        /// detach calls are API misuse and assert in debug builds.
        [[nodiscard]] bool Detach();

        /// Returns true when the owned thread can be joined or detached.
        [[nodiscard]] bool IsJoinable() const noexcept;

        /// Returns the last known id of the owned thread. A default id is returned when no thread is owned.
        [[nodiscard]] ThreadId GetId() const noexcept;

        /// Returns the diagnostic name copied from the descriptor used to start this thread.
        [[nodiscard]] std::string_view GetName() const noexcept;

    private:
        using ThreadFunction = std::function<void()>;

        [[nodiscard]] ErrorCode StartFunction(const ThreadDesc& desc, ThreadFunction function);

    private:
        struct State;
        std::unique_ptr<State> state_;
    };

    /// Returns the VEngine id for the calling thread.
    [[nodiscard]] ThreadId GetCurrentThreadId() noexcept;

    /// Returns the last VEngine-known name for the calling thread.
    ///
    /// The returned value is empty when the thread has not been named through VEngine. The value is diagnostic only and
    /// does not imply that platform debugger naming succeeded.
    [[nodiscard]] std::string GetCurrentThreadName();

    /// Stores a VEngine-known name for the calling thread and applies the platform debugger name where supported.
    ///
    /// Platform naming failures are ignored because thread names are diagnostic labels, not runtime correctness state.
    void SetCurrentThreadName(std::string_view name);

    /// Blocks the calling thread for at least the requested duration.
    ///
    /// This helper is a thin VEngine-owned wrapper over std::this_thread::sleep_for so call sites do not depend
    /// directly on standard-library threading APIs.
    void SleepFor(std::chrono::nanoseconds duration);

    /// Hints that the calling thread is willing to let another runnable thread execute.
    void YieldThread() noexcept;
} // namespace ve
