#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <condition_variable>
#include <mutex>
#include <semaphore>

namespace ve
{
/// Tag used to construct a UniqueLock without immediately locking its mutex.
struct DeferLockTag
{
};

inline constexpr DeferLockTag DeferLock{};

/// Non-recursive mutual exclusion primitive.
///
/// Mutex is non-copyable and non-movable. It may block in Lock(). The same thread must not call Lock() twice without an
/// intervening Unlock(); use RecursiveMutex only when recursive acquisition is truly required.
class Mutex : public NonMovable
{
public:
    Mutex() = default;
    ~Mutex() = default;

    /// Blocks until the calling thread acquires the mutex.
    ///
    /// The calling thread must not already own this mutex. Violating that rule is API misuse and may deadlock.
    void Lock();

    /// Attempts to acquire the mutex without blocking.
    ///
    /// Returns true when the calling thread acquired the mutex. Returns false when another thread currently owns it.
    [[nodiscard]] bool TryLock();

    /// Releases the mutex.
    ///
    /// The calling thread must own the mutex before calling Unlock(). Unlocking from another thread is API misuse.
    void Unlock() noexcept;

private:
    friend class UniqueLock;

    std::mutex native_;
};

/// Recursive mutual exclusion primitive.
///
/// RecursiveMutex is non-copyable and non-movable. The owning thread may call Lock() multiple times, but it must call
/// Unlock() once for each successful Lock() or TryLock(). Prefer Mutex unless recursive acquisition is required.
class RecursiveMutex : public NonMovable
{
public:
    RecursiveMutex() = default;
    ~RecursiveMutex() = default;

    /// Blocks until the calling thread acquires one recursive ownership level.
    void Lock();

    /// Attempts to acquire one recursive ownership level without blocking.
    [[nodiscard]] bool TryLock();

    /// Releases one recursive ownership level.
    ///
    /// The calling thread must own at least one recursive level before calling Unlock().
    void Unlock() noexcept;

private:
    std::recursive_mutex native_;
};

/// RAII helper for a short lexical lock scope.
///
/// LockGuard locks the supplied mutex in its constructor and unlocks it in its destructor. It is non-copyable and
/// non-movable, and it does not support manual unlocking.
template <typename TMutex>
class LockGuard : public NonMovable
{
public:
    /// Locks the supplied mutex and owns it until this guard is destroyed.
    explicit LockGuard(TMutex& mutex)
        : mutex_(mutex)
    {
        mutex_.Lock();
    }

    /// Unlocks the owned mutex.
    ~LockGuard()
    {
        mutex_.Unlock();
    }

private:
    TMutex& mutex_;
};

/// RAII helper for mutex ownership that can be manually unlocked and relocked.
///
/// UniqueLock owns at most one Mutex lock. It may be constructed locked or deferred. ConditionVariable waits require a
/// UniqueLock so the wait operation can atomically release and reacquire the mutex.
class UniqueLock : public NonMovable
{
public:
    /// Locks the mutex immediately.
    explicit UniqueLock(Mutex& mutex);

    /// Stores the mutex without locking it.
    UniqueLock(Mutex& mutex, DeferLockTag);

    /// Unlocks the mutex when this lock currently owns it.
    ~UniqueLock();

    /// Blocks until this lock acquires its mutex.
    ///
    /// Calling Lock() while this object already owns the mutex is API misuse.
    void Lock();

    /// Attempts to acquire the mutex without blocking.
    ///
    /// Returns true when the mutex was acquired. Calling TryLock() while already owning the mutex is API misuse.
    [[nodiscard]] bool TryLock();

    /// Releases the mutex.
    ///
    /// Calling Unlock() when this object does not own the mutex is API misuse.
    void Unlock();

    /// Returns true when this object currently owns the mutex.
    [[nodiscard]] bool OwnsLock() const noexcept;

private:
    friend class ConditionVariable;

    std::unique_lock<std::mutex> native_;
};

/// Condition variable for blocking until shared state changes.
///
/// ConditionVariable is non-copyable and non-movable. Wait() can wake spuriously, so predicate waits are preferred.
/// Callers must hold the associated UniqueLock before waiting.
class ConditionVariable : public NonMovable
{
public:
    ConditionVariable() = default;
    ~ConditionVariable() = default;

    /// Blocks until notified, then reacquires the supplied lock before returning.
    ///
    /// This wait may wake spuriously. Callers should usually use the predicate overload.
    void Wait(UniqueLock& lock);

    /// Blocks until the predicate returns true while the supplied lock is held.
    ///
    /// This overload handles spurious wakeups by re-checking the predicate after each wake.
    template <typename Predicate>
    void Wait(UniqueLock& lock, Predicate predicate)
    {
        native_.wait(lock.native_, predicate);
    }

    /// Wakes one waiting thread, if any.
    void NotifyOne() noexcept;

    /// Wakes all waiting threads.
    void NotifyAll() noexcept;

private:
    std::condition_variable native_;
};

/// Counting semaphore.
///
/// Semaphore stores a non-negative count. Acquire() blocks until the count is greater than zero, then consumes one
/// count. Release() increments the count and may wake waiting threads.
class Semaphore : public NonMovable
{
public:
    /// Creates a semaphore with the supplied initial count.
    explicit Semaphore(SizeT initialCount = 0);
    ~Semaphore() = default;

    /// Increments the semaphore by count and wakes waiters as needed.
    ///
    /// count must be greater than zero and must fit in the platform semaphore counter range.
    void Release(SizeT count = 1);

    /// Blocks until a count is available, then consumes one count.
    void Acquire();

    /// Attempts to consume one count without blocking.
    ///
    /// Returns true when a count was consumed. Returns false when the current count is zero.
    [[nodiscard]] bool TryAcquire();

private:
    std::counting_semaphore<> native_;
};

/// Level-triggered event that stays signaled until Reset() is called.
///
/// ManualResetEvent is non-copyable and non-movable. Set() wakes all current waiters and lets future waiters pass until
/// Reset() clears the signaled state.
class ManualResetEvent : public NonMovable
{
public:
    /// Creates the event in either the signaled or non-signaled state.
    explicit ManualResetEvent(bool signaled = false);
    ~ManualResetEvent() = default;

    /// Sets the event to signaled and wakes all waiting threads.
    void Set();

    /// Clears the signaled state. Future Wait() calls block until Set() is called again.
    void Reset();

    /// Blocks until the event is signaled.
    void Wait();

    /// Returns the current signaled state without blocking.
    [[nodiscard]] bool TryWait();

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool signaled_ = false;
};

/// Event that releases at most one waiter for each stored signal.
///
/// AutoResetEvent is non-copyable and non-movable. Set() wakes one waiter when possible. If no waiter consumes the
/// signal immediately, one signaled state is stored. Multiple Set() calls may coalesce into one stored signal.
class AutoResetEvent : public NonMovable
{
public:
    /// Creates the event in either the signaled or non-signaled state.
    explicit AutoResetEvent(bool signaled = false);
    ~AutoResetEvent() = default;

    /// Stores a signal and wakes one waiting thread, if any.
    void Set();

    /// Clears any stored signal.
    void Reset();

    /// Blocks until one signal is available, then consumes it.
    void Wait();

    /// Consumes one signal without blocking when a signal is available.
    ///
    /// Returns true when a signal was consumed. Returns false when the event was non-signaled.
    [[nodiscard]] bool TryWait();

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    bool signaled_ = false;
};
}
