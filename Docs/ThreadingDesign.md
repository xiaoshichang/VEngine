# Threading Design

## 1. Purpose

The `Threading` module provides VEngine-owned cross-platform wrappers for threads, synchronization primitives, and a
small set of lock-free utilities.

The first-stage goal is not to build a full task runtime. The goal is to establish a clear, documented foundation that
the later JobSystem, IOSystem, Render Thread, Resource system, and tests can use without exposing platform APIs or
standard-library threading types throughout engine code.

## 2. Design Decisions

The first-stage `Threading` design follows these decisions:

- Use C++20 standard-library primitives first.
- Add platform helpers only where standard C++ does not provide the required engine behavior, such as thread naming.
- Keep the public VEngine API self-owned. Public functions should not take or return `std::thread`, platform thread
  handles, `HANDLE`, `pthread_t`, or platform SDK types.
- Place cross-platform public APIs under `Engine/Runtime/Threading`.
- Use namespace `ve`.
- Keep lifecycle explicit. Threads must be joined or detached by the owner.
- Provide synchronization primitives as VEngine wrappers so future engine systems can avoid depending directly on
  standard-library details.
- Provide only simple lock-free wrappers in the first stage.
- Document every public type and public function strictly.
- Document every lock-free memory-ordering decision at the implementation site.

## 3. Goals

First-stage scope:

- `Thread` wrapper.
- Thread descriptors, thread ids, and thread naming.
- Sleep and yield helpers.
- Mutex and recursive mutex wrappers.
- Scoped lock helpers.
- Condition variable wrapper.
- Counting semaphore wrapper.
- Manual-reset and auto-reset event wrappers.
- Atomic wrapper aliases or thin wrappers.
- Fixed-capacity single-producer single-consumer lock-free queue.
- Unit tests for the public behavior.

The first implementation should be small enough to read in one sitting. The API should prefer explicit, boring behavior
over clever abstractions.

## 4. Non-Goals

The first-stage implementation does not include:

- Thread priority.
- Thread affinity.
- Stack size selection.
- Cancellation tokens.
- Suspend or resume operations.
- Public native thread handle exposure.
- `SharedMutex`.
- `SpinLock`.
- `ReadWriteLock`.
- Barrier or latch primitives.
- MPSC or MPMC lock-free queues.
- Lock-free stacks or intrusive lock-free lists.
- Hazard pointers, epochs, or custom lock-free memory reclamation.
- A JobSystem implementation.
- An IOSystem implementation.

JobSystem and IOSystem work should build on this module later.

## 5. Module Boundaries

Allowed first-stage dependencies:

```text
Engine/Runtime/Core
C++20 standard library
Small platform helpers hidden in .cpp files
```

Forbidden public dependencies:

```text
Windows SDK types
pthread types
Objective-C or Objective-C++ runtime types
std::thread as a public parameter or return type
std::mutex as a public parameter or return type
Boost threading APIs
Logging module
RHI
Scene
Resource
Editor
Player
```

The `Threading` module should avoid depending on the `Logging` module. Logging may later choose to depend on Threading
wrappers, but Threading should stay lower-level than Logging to avoid a dependency cycle.

## 6. Planned File Layout

The first implementation should add only files that contain useful code:

```text
Engine/Runtime/Threading/Thread.h
Engine/Runtime/Threading/Thread.cpp
Engine/Runtime/Threading/Synchronization.h
Engine/Runtime/Threading/Synchronization.cpp
Engine/Runtime/Threading/Atomic.h
Engine/Runtime/Threading/LockFreeSpscQueue.h

Tests/Unit/ThreadingTests.cpp
```

Platform-specific helper files may be added only when needed:

```text
Engine/Runtime/Threading/Windows/Win32Threading.h
Engine/Runtime/Threading/Windows/Win32Threading.cpp
Engine/Runtime/Threading/Apple/AppleThreading.h
Engine/Runtime/Threading/Apple/AppleThreading.mm
```

The public cross-platform headers must remain usable without including platform SDK headers.

## 7. Thread Wrapper

### 7.1 Public Concepts

The first-stage API should expose:

```text
Thread
ThreadDesc
ThreadId
GetCurrentThreadId()
GetCurrentThreadName()
SetCurrentThreadName()
SleepFor()
YieldThread()
```

`ThreadDesc` should initially contain only data that is supported in the first implementation:

```cpp
struct ThreadDesc
{
    std::string name;
};
```

The name is copied by `Thread::Start()` so callers do not need to keep the source string alive.

### 7.2 Thread Lifecycle

`Thread` owns at most one running or joinable thread at a time.

Required behavior:

- `Start()` returns `Result<void>`.
- Starting an already-started or still-joinable thread returns `ErrorCode::InvalidState`.
- Starting with an invalid callable returns `ErrorCode::InvalidArgument` when the API can detect it.
- Platform creation failure returns `ErrorCode::PlatformError`.
- `Join()` returns `true` when it joins a joinable thread.
- `Join()` returns `false` when the thread is not joinable and should assert in debug builds.
- `Detach()` returns `true` when it detaches a joinable thread.
- `Detach()` returns `false` when the thread is not joinable and should assert in debug builds.
- The destructor asserts in debug builds if the thread is still joinable.
- The destructor must not automatically join.
- The destructor must not automatically detach.

The owner of a thread is responsible for making the lifecycle explicit:

```text
Start -> Join
Start -> Detach
```

Automatic join can hide stalls during destruction. Automatic detach can hide lifetime bugs. The first implementation
should avoid both.

### 7.3 Thread Function Rules

Thread functions must not throw exceptions across the thread entry boundary.

If a user thread function throws and the implementation does not catch it, C++ will terminate the process. This is
acceptable for the first stage because normal VEngine runtime errors should flow through `Result<T>` or explicit state,
not through uncaught exceptions.

### 7.4 Thread Naming

Thread names are diagnostic labels. They are useful for debuggers, profilers, logs, tests, and future editor
diagnostics.

Rules:

- `ThreadDesc::name` is optional.
- Empty names are allowed.
- `Thread::GetName()` returns the copied descriptor name for the owned thread.
- `SetCurrentThreadName()` sets the name for the calling thread where supported.
- `GetCurrentThreadName()` returns the last VEngine-known name for the calling thread when available.
- Platform naming failure should not make thread creation fail unless the platform API failure indicates a serious
  creation error.

Implementation intent:

```text
Windows
  Use SetThreadDescription where available.

iOS / Apple
  Use pthread_setname_np for the current thread where available.

Other platforms
  Keep the VEngine-known name even if the platform debugger name cannot be set.
```

### 7.5 Priority And Affinity Future Extensions

Priority and affinity are not first-stage features, but the first-stage design must leave room for them.

The public API should not expose native handles. Future priority and affinity should be added through `ThreadDesc` and
internal platform helpers:

```cpp
enum class ThreadPriority
{
    Default,
    Low,
    Normal,
    High,
    TimeCritical,
};

struct ThreadAffinity
{
    // Future platform-neutral affinity description.
};

struct ThreadDesc
{
    std::string name;
    ThreadPriority priority = ThreadPriority::Default;
    ThreadAffinity affinity;
};
```

The first implementation may create threads using `std::thread` and then apply platform attributes through
`native_handle()` internally. This is source-compatible with future public APIs as long as `std::thread` is not exposed
as a public parameter or return type.

If future code needs to guarantee that priority or affinity is applied before user code begins running, `Thread` can add
an internal start gate:

```text
Parent thread creates worker.
Worker blocks before calling user function.
Parent applies platform attributes.
Parent releases worker start gate.
Worker calls user function.
```

Alternatively, specific platforms may use platform-native creation paths internally. This should not require changing
normal `Thread` call sites.

Affinity is platform-dependent and may be best-effort or unsupported on some targets. Future APIs should be able to
return `ErrorCode::Unsupported` when a requested affinity cannot be expressed on the current platform.

## 8. Synchronization Primitives

### 8.1 Mutex

`Mutex` is a non-recursive mutual exclusion primitive.

Required public behavior:

- `Lock()` blocks until the mutex is acquired.
- `TryLock()` returns immediately.
- `Unlock()` releases the mutex.
- `Mutex` is not copyable.
- `Mutex` is not movable.

Documentation requirements:

- The class comment must state that `Mutex` is non-recursive.
- `Lock()` must document that calling it twice from the same thread without `Unlock()` is invalid.
- `Unlock()` must document that the calling thread must own the mutex.

### 8.2 RecursiveMutex

`RecursiveMutex` allows the owning thread to acquire the same mutex more than once.

Required public behavior:

- `Lock()` blocks until the recursive mutex is acquired.
- `TryLock()` returns immediately.
- `Unlock()` releases one recursive acquisition level.
- `RecursiveMutex` is not copyable.
- `RecursiveMutex` is not movable.

Use `RecursiveMutex` sparingly. Most engine systems should prefer `Mutex` and simpler ownership rules.

### 8.3 LockGuard

`LockGuard<TMutex>` is an RAII helper for simple scoped locking.

Required behavior:

- Constructor locks the supplied mutex.
- Destructor unlocks the supplied mutex.
- It cannot be copied.
- It cannot be moved.
- It does not support manual unlock.

Documentation must state that `LockGuard` is for short lexical scopes.

### 8.4 UniqueLock

`UniqueLock` is an RAII helper for mutex ownership that can be manually unlocked and relocked.

Required behavior:

- Constructor can lock immediately.
- Constructor can adopt a deferred lock state if needed by the implementation.
- `Lock()` acquires the mutex.
- `TryLock()` attempts to acquire the mutex.
- `Unlock()` releases the mutex.
- `OwnsLock()` reports whether the lock currently owns the mutex.
- Destructor unlocks only when it owns the mutex.

`ConditionVariable` should use `UniqueLock`.

### 8.5 ConditionVariable

`ConditionVariable` blocks one or more waiting threads until notified.

Required public behavior:

- `Wait(UniqueLock& lock)` waits and may wake spuriously.
- Predicate-based `Wait()` should be provided so common code can avoid incorrect spurious-wakeup handling.
- `NotifyOne()` wakes one waiter.
- `NotifyAll()` wakes all waiters.

Documentation requirements:

- `Wait()` must state that spurious wakeups are possible.
- Predicate `Wait()` must be the preferred overload in examples and tests.
- The caller must hold the associated mutex before waiting.

### 8.6 Semaphore

`Semaphore` is a counting semaphore.

Required public behavior:

- Construct with an initial count.
- `Release(count)` increments the semaphore by `count`.
- `Acquire()` blocks until a count is available and consumes one count.
- `TryAcquire()` returns immediately.

Documentation requirements:

- The class comment must define the count behavior.
- `Release()` must document valid count ranges.
- `Acquire()` must document that it may block.

### 8.7 ManualResetEvent

`ManualResetEvent` is a level-triggered event.

Required public behavior:

- Construct as signaled or non-signaled.
- `Set()` puts the event into the signaled state and wakes all waiters.
- `Reset()` puts the event into the non-signaled state.
- `Wait()` blocks until the event is signaled.
- `TryWait()` returns the current signaled state without blocking.

When signaled, all current and future waiters pass until `Reset()` is called.

### 8.8 AutoResetEvent

`AutoResetEvent` is an event that releases at most one waiter for each signal.

Required public behavior:

- Construct as signaled or non-signaled.
- `Set()` wakes one waiter if one exists.
- `Set()` stores a single signaled state if no waiter exists.
- `Reset()` clears the signaled state.
- `Wait()` blocks until it consumes a signal.
- `TryWait()` consumes a signal if one is available.

Multiple `Set()` calls may coalesce into one stored signal when there are no waiters. This behavior must be documented.

## 9. Atomic Utilities

Atomic utilities should provide an engine-owned surface over `std::atomic`.

First-stage options:

```text
Atomic<T>
AtomicBool
AtomicInt32
AtomicUInt32
AtomicSize
```

The wrapper may be a thin class over `std::atomic<T>` or a documented alias set if that keeps the implementation
simpler. If aliases are used, the lock-free queue implementation must still document all memory-order choices at each
atomic operation site.

Preferred wrapper operations:

```text
Load(order)
Store(value, order)
Exchange(value, order)
CompareExchangeWeak(expected, desired, successOrder, failureOrder)
CompareExchangeStrong(expected, desired, successOrder, failureOrder)
FetchAdd(value, order)
FetchSub(value, order)
```

Atomic APIs must make memory ordering visible. Call sites should avoid relying on hidden default ordering in code where
ordering matters.

## 10. Lock-Free SPSC Queue

### 10.1 Scope

The first lock-free queue should be a fixed-capacity single-producer single-consumer queue:

```cpp
template <typename T, SizeT Capacity>
class LockFreeSpscQueue;
```

This queue is intended for simple cross-thread pipes where ownership is clear:

```text
Game Thread -> Render Thread command pipe
IOSystem-owned IO Thread -> ResourceSystem or Game Thread completion pipe
Producer test thread -> Consumer test thread
```

It is not an MPSC or MPMC queue. Using multiple producer threads or multiple consumer threads is invalid.

The current IOSystem first version exposes a global completion queue that is polled by whichever thread owns result
handling. A dedicated Game Thread completion pipe is a future ResourceSystem or scheduling-layer policy, not a guarantee
made by low-level Threading or IOSystem.

### 10.2 Required Behavior

Required public behavior:

- `TryPush(const T& value)` returns `false` when the queue is full.
- `TryPush(T&& value)` returns `false` when the queue is full.
- `TryPop(T& outValue)` returns `false` when the queue is empty.
- `IsEmpty()` returns a best-effort current state.
- `IsFull()` returns a best-effort current state.
- `GetCapacity()` returns `Capacity`.

The queue must not allocate memory at runtime. Capacity is fixed by the template parameter.

### 10.3 Type Requirements

The first implementation may require `T` to be:

```text
Copy assignable for TryPush(const T&)
Move assignable for TryPush(T&&)
Default constructible if the storage uses a fixed array of T
```

If the implementation uses manual storage later, these requirements may be relaxed. The first implementation should keep
the type requirements simple and document them in the class comment.

### 10.4 Memory Ordering Policy

The SPSC queue should use a ring buffer with separate producer and consumer indices.

Expected ordering:

```text
Producer:
  Load local tail with relaxed ordering.
  Load consumer head with acquire ordering to observe released consumer progress.
  Write item into the tail slot.
  Store new tail with release ordering to publish the item.

Consumer:
  Load local head with relaxed ordering.
  Load producer tail with acquire ordering to observe released producer progress.
  Read item from the head slot.
  Store new head with release ordering to publish consumed space.
```

Every acquire, release, and relaxed operation must have a nearby implementation comment explaining why that ordering is
valid in the SPSC model.

Required implementation comments:

- The producer is the only writer of `tail`.
- The consumer is the only writer of `head`.
- The producer writes item storage before publishing `tail` with release ordering.
- The consumer reads published item storage only after acquiring `tail`.
- The consumer publishes free space by storing `head` with release ordering.
- The producer observes free space by acquiring `head`.

The queue should not use `memory_order_seq_cst` unless a simpler proof is deliberately chosen and documented.

## 11. Commenting Requirements

Threading code must be documented more strictly than ordinary utility code because concurrency bugs are hard to inspect.

Every public type must document:

- What the type owns.
- Whether it can be copied or moved.
- Whether it may block.
- Thread-safety rules.
- Lifetime rules.

Every public function must document:

- Preconditions.
- Return value.
- Blocking behavior.
- Whether the function can be called concurrently.
- Whether failure indicates runtime state or API misuse.

Every wait or signal primitive must document:

- Whether waits can wake spuriously.
- Whether signals are level-triggered or edge-triggered.
- Whether signals are stored when no waiter exists.
- Whether multiple signals can coalesce.

Every lock-free type must document:

- Producer and consumer count.
- Ownership of each atomic index.
- Runtime allocation behavior.
- Type requirements.
- Memory-ordering rules.

Every non-`seq_cst` atomic operation in implementation code must have a local comment describing the ordering.

Comments should be in English to match the rest of the repository documentation and public C++ comments.

## 12. Testing Plan

Add a `VEngineThreadingTests` executable through CMake/CTest.

Thread wrapper tests:

- Start and join a thread.
- Verify a thread can write to shared test state through synchronized access.
- Verify repeated `Start()` on a running or joinable thread fails.
- Verify `Join()` on a non-joinable thread returns `false`.
- Verify `Detach()` on a non-joinable thread returns `false`.
- Verify current thread naming stores and returns a VEngine-known name.

Synchronization tests:

- `Mutex` protects a counter across multiple threads.
- `RecursiveMutex` supports recursive acquisition by one thread.
- `LockGuard` unlocks at scope exit.
- `UniqueLock` can lock, unlock, and report ownership.
- `ConditionVariable` wakes a waiting thread through a predicate.
- `Semaphore` blocks until release and consumes counts.
- `ManualResetEvent` wakes all waiters and stays signaled until reset.
- `AutoResetEvent` releases one waiter and consumes one stored signal.

Lock-free tests:

- Empty SPSC queue returns `false` from `TryPop()`.
- Full SPSC queue returns `false` from `TryPush()`.
- Interleaved push and pop preserve FIFO order.
- A producer thread and consumer thread transfer a sequence of values without loss.
- `IsEmpty()` and `IsFull()` behave correctly in single-threaded checks.

Tests should avoid timing-sensitive sleeps where possible. Prefer events, semaphores, atomics, or condition variables to
coordinate test threads.

## 13. Future Work

Future extensions may include:

- Thread priority.
- Thread affinity.
- Stack size selection.
- `SharedMutex`.
- `SpinLock`.
- Barriers and latches.
- MPSC queues.
- MPMC queues.
- Boost.Lockfree-backed wrappers where appropriate.
- JobSystem.
- IOSystem.
- Thread profiling integration.

Priority and affinity should be added through `ThreadDesc` and hidden platform helpers. They should not require normal
callers to change from `Thread::Start(desc, callable)` and `Thread::Join()`.
