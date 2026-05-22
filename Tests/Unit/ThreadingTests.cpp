#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/LockFreeSpscQueue.h"
#include "Engine/Runtime/Threading/Synchronization.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <iostream>
#include <string_view>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }

    return condition;
}

bool ExpectOk(const ve::Result<void>& result, const char* message)
{
    if (result)
    {
        return true;
    }

    std::cerr << "FAILED: " << message << ": " << ve::ToString(result.GetError().GetCode());

    if (!result.GetError().GetMessage().empty())
    {
        std::cerr << ": " << result.GetError().GetMessage();
    }

    std::cerr << '\n';
    return false;
}

bool TestThreadStartJoinAndName()
{
    bool passed = true;

    ve::ManualResetEvent workerFinished;
    ve::Mutex mutex;
    std::string workerName;
    ve::ThreadId workerThreadId;
    int sharedValue = 0;

    ve::Thread thread;
    const ve::Result<void> startResult = thread.Start(ve::ThreadDesc{"ThreadingTestWorker"}, [&]()
    {
        {
            ve::LockGuard lock(mutex);
            workerName = ve::GetCurrentThreadName();
            workerThreadId = ve::GetCurrentThreadId();
            sharedValue = 42;
        }

        workerFinished.Set();
    });

    passed &= ExpectOk(startResult, "Thread should start");
    passed &= Expect(thread.IsJoinable(), "Started thread should be joinable");
    passed &= Expect(thread.GetId().IsValid(), "Started thread should expose a valid id");
    passed &= Expect(thread.GetName() == std::string_view("ThreadingTestWorker"), "Thread should preserve its name");

    workerFinished.Wait();
    passed &= Expect(thread.Join(), "Join should succeed for a joinable thread");
    passed &= Expect(!thread.IsJoinable(), "Joined thread should not remain joinable");

    {
        ve::LockGuard lock(mutex);
        passed &= Expect(workerName == "ThreadingTestWorker", "Worker should see the VEngine thread name");
        passed &= Expect(workerThreadId.IsValid(), "Worker should see a valid current thread id");
        passed &= Expect(sharedValue == 42, "Worker should update synchronized state");
    }

    return passed;
}

bool TestThreadRejectsRepeatedStart()
{
    bool passed = true;

    ve::ManualResetEvent releaseWorker;
    ve::Thread thread;

    passed &= ExpectOk(
        thread.Start(ve::ThreadDesc{"RepeatedStartWorker"}, [&]()
        {
            releaseWorker.Wait();
        }),
        "Initial thread start should succeed");

    const ve::Result<void> repeatedStart = thread.Start(ve::ThreadDesc{"ShouldFail"}, []()
    {
    });

    passed &= Expect(!repeatedStart, "Repeated Start should fail while the thread is joinable");
    if (!repeatedStart)
    {
        passed &= Expect(
            repeatedStart.GetError().GetCode() == ve::ErrorCode::InvalidState,
            "Repeated Start should report InvalidState");
    }

    releaseWorker.Set();
    passed &= Expect(thread.Join(), "Join should succeed after repeated Start failure");

    return passed;
}

bool TestMutexAndLockGuard()
{
    bool passed = true;

    constexpr int ThreadCount = 4;
    constexpr int IterationsPerThread = 1000;

    ve::Mutex mutex;
    ve::Thread threads[ThreadCount];
    int counter = 0;

    for (int threadIndex = 0; threadIndex < ThreadCount; ++threadIndex)
    {
        passed &= ExpectOk(
            threads[threadIndex].Start(ve::ThreadDesc{"CounterWorker"}, [&]()
            {
                for (int iteration = 0; iteration < IterationsPerThread; ++iteration)
                {
                    ve::LockGuard lock(mutex);
                    ++counter;
                }
            }),
            "Counter worker should start");
    }

    for (ve::Thread& thread : threads)
    {
        passed &= Expect(thread.Join(), "Counter worker should join");
    }

    passed &= Expect(
        counter == ThreadCount * IterationsPerThread,
        "Mutex and LockGuard should protect shared counter updates");

    return passed;
}

bool TestRecursiveMutex()
{
    bool passed = true;

    ve::RecursiveMutex mutex;

    mutex.Lock();
    mutex.Lock();
    mutex.Unlock();
    mutex.Unlock();

    passed &= Expect(mutex.TryLock(), "RecursiveMutex should be available after balanced unlocks");
    mutex.Unlock();

    return passed;
}

bool TestUniqueLock()
{
    bool passed = true;

    ve::Mutex mutex;
    ve::UniqueLock lock(mutex, ve::DeferLock);

    passed &= Expect(!lock.OwnsLock(), "Deferred UniqueLock should not own the mutex");
    lock.Lock();
    passed &= Expect(lock.OwnsLock(), "UniqueLock should own the mutex after Lock");
    lock.Unlock();
    passed &= Expect(!lock.OwnsLock(), "UniqueLock should not own the mutex after Unlock");
    passed &= Expect(lock.TryLock(), "UniqueLock TryLock should acquire an unlocked mutex");

    return passed;
}

bool TestConditionVariable()
{
    bool passed = true;

    ve::Mutex mutex;
    ve::ConditionVariable condition;
    ve::ManualResetEvent waiterStarted;
    bool ready = false;
    bool observedReady = false;

    ve::Thread waiter;
    passed &= ExpectOk(
        waiter.Start(ve::ThreadDesc{"ConditionWaiter"}, [&]()
        {
            ve::UniqueLock lock(mutex);
            waiterStarted.Set();
            condition.Wait(lock, [&]()
            {
                return ready;
            });
            observedReady = ready;
        }),
        "Condition waiter should start");

    waiterStarted.Wait();
    {
        ve::LockGuard lock(mutex);
        ready = true;
    }
    condition.NotifyOne();

    passed &= Expect(waiter.Join(), "Condition waiter should join");
    passed &= Expect(observedReady, "Condition waiter should observe predicate state");

    return passed;
}

bool TestSemaphore()
{
    bool passed = true;

    ve::Semaphore semaphore(0);
    ve::ManualResetEvent acquired;
    ve::AtomicInt32 value{0};

    ve::Thread worker;
    passed &= ExpectOk(
        worker.Start(ve::ThreadDesc{"SemaphoreWorker"}, [&]()
        {
            semaphore.Acquire();
            value.store(7, std::memory_order_release);
            acquired.Set();
        }),
        "Semaphore worker should start");

    passed &= Expect(!semaphore.TryAcquire(), "Empty semaphore TryAcquire should fail");
    semaphore.Release();
    acquired.Wait();
    passed &= Expect(value.load(std::memory_order_acquire) == 7, "Semaphore should release the worker");
    passed &= Expect(worker.Join(), "Semaphore worker should join");

    return passed;
}

bool TestManualResetEvent()
{
    bool passed = true;

    ve::ManualResetEvent event;
    ve::AtomicInt32 wakeCount{0};
    ve::Thread first;
    ve::Thread second;

    passed &= ExpectOk(
        first.Start(ve::ThreadDesc{"ManualEventFirst"}, [&]()
        {
            event.Wait();
            wakeCount.fetch_add(1, std::memory_order_acq_rel);
        }),
        "First manual event waiter should start");

    passed &= ExpectOk(
        second.Start(ve::ThreadDesc{"ManualEventSecond"}, [&]()
        {
            event.Wait();
            wakeCount.fetch_add(1, std::memory_order_acq_rel);
        }),
        "Second manual event waiter should start");

    event.Set();
    passed &= Expect(first.Join(), "First manual event waiter should join");
    passed &= Expect(second.Join(), "Second manual event waiter should join");
    passed &= Expect(wakeCount.load(std::memory_order_acquire) == 2, "ManualResetEvent should wake all waiters");
    passed &= Expect(event.TryWait(), "ManualResetEvent should stay signaled after Set");

    event.Reset();
    passed &= Expect(!event.TryWait(), "ManualResetEvent should clear signaled state after Reset");

    return passed;
}

bool TestAutoResetEvent()
{
    bool passed = true;

    ve::AutoResetEvent event;
    ve::AtomicInt32 wakeCount{0};
    ve::Thread worker;

    passed &= ExpectOk(
        worker.Start(ve::ThreadDesc{"AutoEventWorker"}, [&]()
        {
            event.Wait();
            wakeCount.fetch_add(1, std::memory_order_acq_rel);
        }),
        "Auto event worker should start");

    event.Set();
    passed &= Expect(worker.Join(), "Auto event worker should join");
    passed &= Expect(wakeCount.load(std::memory_order_acquire) == 1, "AutoResetEvent should wake one waiter");
    passed &= Expect(!event.TryWait(), "AutoResetEvent signal should be consumed by Wait");

    event.Set();
    event.Set();
    passed &= Expect(event.TryWait(), "AutoResetEvent should store one signal");
    passed &= Expect(!event.TryWait(), "AutoResetEvent should coalesce stored signals");

    return passed;
}

bool TestLockFreeSpscQueueSingleThread()
{
    bool passed = true;

    ve::LockFreeSpscQueue<int, 3> queue;
    int value = 0;

    passed &= Expect(queue.GetCapacity() == 3, "SPSC queue should report fixed capacity");
    passed &= Expect(queue.IsEmpty(), "New SPSC queue should be empty");
    passed &= Expect(!queue.TryPop(value), "Empty SPSC queue should reject pop");

    passed &= Expect(queue.TryPush(1), "SPSC queue should push first value");
    passed &= Expect(queue.TryPush(2), "SPSC queue should push second value");
    passed &= Expect(queue.TryPush(3), "SPSC queue should push third value");
    passed &= Expect(queue.IsFull(), "SPSC queue should report full state");
    passed &= Expect(!queue.TryPush(4), "Full SPSC queue should reject push");

    passed &= Expect(queue.TryPop(value) && value == 1, "SPSC queue should pop first value");
    passed &= Expect(queue.TryPop(value) && value == 2, "SPSC queue should pop second value");
    passed &= Expect(queue.TryPop(value) && value == 3, "SPSC queue should pop third value");
    passed &= Expect(queue.IsEmpty(), "SPSC queue should be empty after pops");

    return passed;
}

bool TestLockFreeSpscQueueCrossThread()
{
    bool passed = true;

    constexpr int ValueCount = 10000;
    ve::LockFreeSpscQueue<int, 64> queue;
    ve::AtomicInt32 consumedCount{0};
    ve::AtomicInt32 consumedSum{0};

    ve::Thread producer;
    ve::Thread consumer;

    passed &= ExpectOk(
        consumer.Start(ve::ThreadDesc{"SpscConsumer"}, [&]()
        {
            int expected = 1;
            while (expected <= ValueCount)
            {
                int value = 0;
                if (queue.TryPop(value))
                {
                    if (value == expected)
                    {
                        consumedSum.fetch_add(value, std::memory_order_acq_rel);
                        consumedCount.fetch_add(1, std::memory_order_acq_rel);
                        ++expected;
                    }
                    else
                    {
                        consumedCount.store(-1, std::memory_order_release);
                        return;
                    }
                }
                else
                {
                    ve::YieldThread();
                }
            }
        }),
        "SPSC consumer should start");

    passed &= ExpectOk(
        producer.Start(ve::ThreadDesc{"SpscProducer"}, [&]()
        {
            for (int value = 1; value <= ValueCount; ++value)
            {
                while (!queue.TryPush(value))
                {
                    ve::YieldThread();
                }
            }
        }),
        "SPSC producer should start");

    passed &= Expect(producer.Join(), "SPSC producer should join");
    passed &= Expect(consumer.Join(), "SPSC consumer should join");

    const int expectedSum = (ValueCount * (ValueCount + 1)) / 2;
    passed &= Expect(consumedCount.load(std::memory_order_acquire) == ValueCount, "SPSC should consume all values");
    passed &= Expect(consumedSum.load(std::memory_order_acquire) == expectedSum, "SPSC should preserve FIFO values");

    return passed;
}
}

int main()
{
    bool passed = true;

    passed &= TestThreadStartJoinAndName();
    passed &= TestThreadRejectsRepeatedStart();
    passed &= TestMutexAndLockGuard();
    passed &= TestRecursiveMutex();
    passed &= TestUniqueLock();
    passed &= TestConditionVariable();
    passed &= TestSemaphore();
    passed &= TestManualResetEvent();
    passed &= TestAutoResetEvent();
    passed &= TestLockFreeSpscQueueSingleThread();
    passed &= TestLockFreeSpscQueueCrossThread();

    if (passed)
    {
        std::cout << "VEngineThreadingTests passed" << '\n';
        return 0;
    }

    return 1;
}
