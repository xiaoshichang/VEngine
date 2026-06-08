#include "Engine/Runtime/Threading/Synchronization.h"

#include "Engine/Runtime/Core/Assert.h"

#include <limits>

namespace ve
{
    namespace
    {
        [[nodiscard]] std::ptrdiff_t ToSemaphoreCount(SizeT count) noexcept
        {
            constexpr auto MaxCount = static_cast<SizeT>((std::numeric_limits<std::ptrdiff_t>::max)());
            VE_ASSERT_MESSAGE(count <= MaxCount, "Semaphore count exceeds std::counting_semaphore range.");
            return static_cast<std::ptrdiff_t>(count <= MaxCount ? count : MaxCount);
        }
    } // namespace

    void Mutex::Lock()
    {
        native_.lock();
    }

    bool Mutex::TryLock()
    {
        return native_.try_lock();
    }

    void Mutex::Unlock() noexcept
    {
        native_.unlock();
    }

    void RecursiveMutex::Lock()
    {
        native_.lock();
    }

    bool RecursiveMutex::TryLock()
    {
        return native_.try_lock();
    }

    void RecursiveMutex::Unlock() noexcept
    {
        native_.unlock();
    }

    UniqueLock::UniqueLock(Mutex& mutex)
        : native_(mutex.native_)
    {
    }

    UniqueLock::UniqueLock(Mutex& mutex, DeferLockTag)
        : native_(mutex.native_, std::defer_lock)
    {
    }

    UniqueLock::~UniqueLock() = default;

    void UniqueLock::Lock()
    {
        VE_ASSERT_MESSAGE(!native_.owns_lock(), "UniqueLock::Lock called while already owning the mutex.");
        native_.lock();
    }

    bool UniqueLock::TryLock()
    {
        VE_ASSERT_MESSAGE(!native_.owns_lock(), "UniqueLock::TryLock called while already owning the mutex.");
        return native_.try_lock();
    }

    void UniqueLock::Unlock()
    {
        VE_ASSERT_MESSAGE(native_.owns_lock(), "UniqueLock::Unlock called without owning the mutex.");
        native_.unlock();
    }

    bool UniqueLock::OwnsLock() const noexcept
    {
        return native_.owns_lock();
    }

    void ConditionVariable::Wait(UniqueLock& lock)
    {
        native_.wait(lock.native_);
    }

    void ConditionVariable::NotifyOne() noexcept
    {
        native_.notify_one();
    }

    void ConditionVariable::NotifyAll() noexcept
    {
        native_.notify_all();
    }

    Semaphore::Semaphore(SizeT initialCount)
        : native_(ToSemaphoreCount(initialCount))
    {
    }

    void Semaphore::Release(SizeT count)
    {
        VE_ASSERT_MESSAGE(count > 0, "Semaphore::Release count must be greater than zero.");
        if (count == 0)
        {
            return;
        }

        native_.release(ToSemaphoreCount(count));
    }

    void Semaphore::Acquire()
    {
        native_.acquire();
    }

    bool Semaphore::TryAcquire()
    {
        return native_.try_acquire();
    }

    ManualResetEvent::ManualResetEvent(bool signaled)
        : signaled_(signaled)
    {
    }

    void ManualResetEvent::Set()
    {
        {
            std::lock_guard lock(mutex_);
            signaled_ = true;
        }

        condition_.notify_all();
    }

    void ManualResetEvent::Reset()
    {
        std::lock_guard lock(mutex_);
        signaled_ = false;
    }

    void ManualResetEvent::Wait()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this]() { return signaled_; });
    }

    bool ManualResetEvent::TryWait()
    {
        std::lock_guard lock(mutex_);
        return signaled_;
    }

    AutoResetEvent::AutoResetEvent(bool signaled)
        : signaled_(signaled)
    {
    }

    void AutoResetEvent::Set()
    {
        {
            std::lock_guard lock(mutex_);
            signaled_ = true;
        }

        condition_.notify_one();
    }

    void AutoResetEvent::Reset()
    {
        std::lock_guard lock(mutex_);
        signaled_ = false;
    }

    void AutoResetEvent::Wait()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this]() { return signaled_; });
        signaled_ = false;
    }

    bool AutoResetEvent::TryWait()
    {
        std::lock_guard lock(mutex_);
        if (!signaled_)
        {
            return false;
        }

        signaled_ = false;
        return true;
    }
} // namespace ve
