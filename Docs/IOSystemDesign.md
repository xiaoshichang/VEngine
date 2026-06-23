# VEngine IOSystem Design

## 1. Purpose

`IOSystem` is the first VEngine runtime service for asynchronous file reads. It moves blocking file read operations off
the Main Thread and Game Thread while keeping file path rules and file access behavior owned by the existing
`FileSystem` module.

`IOSystem` is a service name. It owns one dedicated execution thread called the IO Thread. This distinction keeps the
engine service naming consistent with `JobSystem` while preserving the conceptual thread model:

- `JobSystem`: runtime service that owns Worker Threads.
- `IOSystem`: runtime service that owns the IO Thread.
- `EngineRuntime`: lifecycle owner for both systems.

The first version is intentionally small. It is designed to support later ResourceManager and asset loading work, not to
be a general async runtime or a replacement for `JobSystem`.

## 2. Naming Policy

Long-lived services owned by `EngineRuntime` use `System` in their type names:

```text
JobSystem
IOSystem
RenderSystem
SceneSystem
ResourceSystem
InputSystem
ScriptSystem
UISystem
PhysicsSystem
```

Not every future module must land at once, but new runtime-owned service APIs should follow this naming convention.
Conceptual or physical threads continue to use thread names such as IO Thread, Render Thread, Game Thread, and Worker
Threads.

## 3. First Version Scope

The first version supports:

- `IOSystem` lifecycle with `Initialize()` and `Shutdown()`.
- One dedicated IO Thread.
- Asynchronous binary file read requests.
- `IORequestHandle`.
- `Wait()`, `WaitAll()`, and `IsComplete()`.
- Poll-based consumption of completed read results.
- `High` and `Normal` pending request queues.
- Integration into `EngineRuntime`.

The first version does not support:

- Asynchronous text file reads.
- Asynchronous file writes.
- Request cancellation.
- Callbacks.
- Completion dispatch back to the submitting thread.
- Per-owner completion queues.
- Named-thread completion routing.
- Direct dispatch into `JobSystem`.
- Platform-native overlapped IO, IOCP, or dispatch IO.
- Directory watching.
- Resource parsing, decoding, importing, or caching.

## 4. Module Boundary

Files:

```text
Engine/Runtime/IO/IOSystem.h
Engine/Runtime/IO/IOSystem.cpp
Tests/Unit/IOSystemTests.cpp
```

Allowed dependencies:

```text
Engine/Runtime/Core
Engine/Runtime/FileSystem
Engine/Runtime/Threading
C++20 standard library
```

`IOSystem` should not depend directly on:

```text
Logging
JobSystem
RHI
Scene
Resource
Editor
Player
```

`EngineRuntime`, which is higher-level than both services, may initialize `IOSystem` and log successful service
startup. This preserves the same dependency direction already used for `JobSystem`.

## 5. Thread Model

An `IOSystem` object owns one `ve::Thread`.

```text
Caller / Game Thread / ResourceSystem
  -> Submit a binary read request
  -> Receive IORequestHandle

IO Thread
  -> Wait for a pending request
  -> Read one file through FileSystem::ReadBinaryFile()
  -> Store a completed result
  -> Notify handle waiters

Caller / Game Thread / ResourceSystem
  -> Poll completed read results
  -> Consume bytes or handle read error
```

The IO Thread processes one file read at a time. A single dedicated thread is enough for the first version because it
establishes correct async boundaries without committing the engine to a platform-specific asynchronous IO implementation
or a multi-disk scheduling policy.

## 6. Public API Shape

The first-stage API should follow this form:

```cpp
enum class IORequestPriority
{
    Normal,
    High,
};

struct IOSystemDesc
{
    std::string threadName = "VEngineIOThread";
};

struct IOReadRequestDesc
{
    Path path;
    IORequestPriority priority = IORequestPriority::Normal;
    std::string debugName;
};

class IORequestHandle
{
public:
    [[nodiscard]] bool IsValid() const noexcept;
    void Reset() noexcept;
};

struct IOReadResult
{
    IORequestHandle handle;
    Path path;
    std::vector<std::byte> data;
    Error error;

    [[nodiscard]] bool IsOk() const noexcept;
};

class IOSystem : public NonMovable
{
public:
    [[nodiscard]] ErrorCode Initialize(const IOSystemDesc& desc);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

    [[nodiscard]] Result<IORequestHandle> ReadBinaryFile(const IOReadRequestDesc& desc);

    [[nodiscard]] bool IsComplete(const IORequestHandle& handle) const noexcept;
    void Wait(const IORequestHandle& handle) const;
    void WaitAll(std::span<const IORequestHandle> handles) const;

    [[nodiscard]] bool TryPopCompletedRead(IOReadResult& outResult);
};
```

`IOReadResult` uses `data` plus `error` rather than nesting `Result<std::vector<std::byte>>` so callers can
default-construct a result for polling and the system can move large read buffers out exactly once. `IsOk()` returns
true when `error` is `ErrorCode::None`.

## 7. Request Lifecycle

Requests move through these states internally:

```text
Pending -> Running -> Complete
```

A completed request may contain either:

```text
Successful read
  data contains file bytes
  error is None

Failed read
  data is empty
  error contains the FileSystem read failure
```

Scheduling failures are separate from read failures:

- `ReadBinaryFile()` returns `InvalidState` if `IOSystem` is not accepting requests.
- `ReadBinaryFile()` returns `InvalidArgument` immediately for an empty path.
- `ReadBinaryFile()` returns `OutOfMemory` if request allocation fails.
- Missing files and file read failures are completed requests with an error in `IOReadResult`.

This separation means a request handle always represents an operation that was accepted by the system.

## 8. Completion Model

Completion is poll-based. The IO Thread does not invoke caller callbacks.

The first-version completed read queue is global to `IOSystem`. Results are not routed back to the thread or system that
submitted the request. The thread that calls `TryPopCompletedRead()` becomes the result-handling thread for that
completion.

```cpp
auto request = ioSystem.ReadBinaryFile(IOReadRequestDesc{Path("Assets/Mesh.vemesh")});

IOReadResult result;
while (!ioSystem.TryPopCompletedRead(result))
{
    // Continue the frame or wait through an explicit synchronization path.
}

if (result.IsOk())
{
    ConsumeBytes(result.data);
}
```

Why polling:

- The IO Thread does not unexpectedly run Scene, Resource, or editor logic.
- The Game Thread or future `ResourceSystem` owns the point where loaded data enters runtime state.
- No Main/Game Thread dispatcher is needed in the first implementation.

Thread-affinity rule:

```text
Submitter thread:
  Schedules the request and receives IORequestHandle.

IO Thread:
  Performs FileSystem::ReadBinaryFile().

Polling thread:
  Calls TryPopCompletedRead().
  Owns result handling for the popped completion.
```

`IOSystem` does not guarantee:

- Return to the submitting thread.
- Return to Game Thread.
- Return to ResourceSystem.
- Return to any named runtime thread.

Until per-owner queues or named-thread dispatchers exist, engine runtime code should choose one owner to poll
`IOSystem` completions. The expected future owner is `ResourceSystem`, polling from its own update point, usually on the
Game Thread. Other systems should avoid independently polling the global completion queue unless they intentionally own
all results they might pop.

`Wait()` and `WaitAll()` are synchronization helpers. They wait for completion but do not consume the corresponding
result from the completed queue. Tests and tools may wait and then poll; normal runtime systems should poll completed
results during their owning update step.

## 9. Priority

The first version supports two request queues:

```cpp
enum class IORequestPriority
{
    Normal,
    High,
};
```

The IO Thread selects queued high-priority requests before queued normal-priority requests. Priority cannot interrupt a
read that is already running.

Suggested meaning:

- `High`: short reads needed to unblock current interactive or loading progress.
- `Normal`: normal asset payload reads and background loading requests.

Large files should not be marked high simply because they are important. A running high-priority large read can still
delay later requests.

## 10. Cancellation And Shutdown

The first version does not expose cancellation.

`Shutdown()` behavior:

```text
Stop accepting new read requests.
Allow queued and running requests to complete.
Wake and stop the IO Thread after the pending queue drains.
Join the IO Thread.
Discard unconsumed completed results during final teardown.
```

This is deliberately deterministic. Platform-independent cancellation of a blocking file read is deferred until there
is a real runtime need and a clear policy for queued versus already-running operations.

Unlike `EngineRuntime`, an isolated `IOSystem` object may be initialized again after shutdown. This matches the
lower-level `JobSystem` lifecycle behavior and keeps unit testing straightforward. `EngineRuntime` remains one-shot and
therefore does not restart its owned systems in normal engine execution.

## 11. Interaction With FileSystem

`IOSystem` is an asynchronous scheduling layer over the existing synchronous `FileSystem` implementation.

```text
IOSystem::ReadBinaryFile()
  -> IO Thread
    -> FileSystem::ReadBinaryFile()
```

Rules:

- Public paths use `ve::Path`.
- Path normalization and project path resolution remain FileSystem concerns.
- `IOSystem` does not expose raw platform file handles or `std::filesystem::path`.
- Text conversion, resource format parsing, and import work are outside IOSystem.

## 12. Interaction With JobSystem And ResourceSystem

`IOSystem` and `JobSystem` are sibling services. `IOSystem` does not directly schedule jobs in the first version.

The current `ResourceSystem` is present, but its resource load path still performs direct synchronous `FileSystem` reads.
The intended async loading flow remains:

```text
ResourceSystem submits read through IOSystem.
ResourceSystem polls completed bytes from its owning thread.
ResourceSystem submits CPU-heavy parsing or conversion through JobSystem when needed.
ResourceSystem publishes completed runtime resources on its owning thread.
```

This keeps responsibilities clear:

- IO Thread performs file reads.
- Worker Threads perform independent CPU work.
- ResourceSystem owns resource state transitions.

## 13. EngineRuntime Integration

`EngineRuntime` owns both systems:

```cpp
struct EngineRuntimeDesc
{
    JobSystemDesc jobSystem;
    IOSystemDesc ioSystem;
};
```

Initialization order:

```text
JobSystem.Initialize()
IOSystem.Initialize()
```

Shutdown order:

```text
IOSystem.Shutdown()
JobSystem.Shutdown()
```

The shutdown order leaves room for future loading flows where IO completion may feed CPU post-processing work while the
runtime is still active.

Player and Editor may provide diagnostic thread names:

```text
VEnginePlayerIOThread
VEngineEditorIOThread
```

`EngineRuntime` logs successful `IOSystem` initialization, as it already does for `JobSystem`, so service modules remain
free of direct Logging dependencies.

## 14. Testing Plan

Add `VEngineIOSystemTests` through CMake/CTest.

Tests should cover:

- Initialize and Shutdown.
- Repeated Initialize while running fails.
- Initialize after Shutdown succeeds for a standalone `IOSystem`.
- Scheduling before initialization or after shutdown fails.
- Binary read completes and returns exact bytes.
- Missing file completes with an error result.
- `Wait()` and `WaitAll()` observe completion.
- `Shutdown()` drains accepted read requests.
- `EngineRuntime` initializes and exposes its owned `IOSystem`.

The first version does not require a timing-sensitive priority order test. Priority queue implementation should be kept
simple, and strict ordering can be tested later when a non-public deterministic test seam is justified.
