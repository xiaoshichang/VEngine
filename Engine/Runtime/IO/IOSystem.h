#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ve
{
struct IORequestState;
struct IOSystemImpl;

/// Controls which pending IO request queue receives a request.
///
/// Priority is an ordering hint for queued requests. A high-priority request is selected before queued normal requests,
/// but it does not interrupt a file read that is already running on the IO Thread.
enum class IORequestPriority
{
    Normal,
    High,
};

/// Describes the dedicated thread created by IOSystem::Initialize().
struct IOSystemDesc
{
    /// Diagnostic name copied into the owned IO Thread.
    std::string threadName = "VEngineIOThread";
};

/// Describes one asynchronous binary file read request.
struct IOReadRequestDesc
{
    /// Engine-facing path to read. Empty paths are rejected before a request is scheduled.
    Path path;

    /// Queue priority used while the request is pending.
    IORequestPriority priority = IORequestPriority::Normal;

    /// Optional diagnostic label for future profiling and debugging. Empty names are allowed.
    std::string debugName;
};

/// Lightweight handle returned for a scheduled IO request.
///
/// A handle may be copied freely and can outlive the IOSystem object that produced it. Waiting on an invalid handle is
/// a no-op, and invalid handles are considered complete.
class IORequestHandle
{
public:
    IORequestHandle() = default;

    /// Returns true when this handle refers to a scheduled request state.
    [[nodiscard]] bool IsValid() const noexcept;

    /// Clears this handle. The underlying request may continue if other owners still reference it.
    void Reset() noexcept;

private:
    friend class IOSystem;

    explicit IORequestHandle(std::shared_ptr<IORequestState> state);

    std::shared_ptr<IORequestState> state_;
};

/// Completed binary read result consumed from IOSystem's completion queue.
struct IOReadResult
{
    /// Handle for the completed request.
    IORequestHandle handle;

    /// Path copied from the original request descriptor.
    Path path;

    /// File bytes when the read succeeds. Empty files produce an empty vector and still report success.
    std::vector<std::byte> data;

    /// Read error. ErrorCode::None means the read succeeded.
    Error error;

    /// Returns true when the read succeeded.
    [[nodiscard]] bool IsOk() const noexcept;
};

/// Owns the asynchronous file IO service used by EngineRuntime and future ResourceSystem code.
///
/// IOSystem owns one dedicated IO Thread. It accepts binary file read requests, executes them through FileSystem on the
/// IO Thread, and exposes completed results through polling. It is not a general worker pool and does not invoke caller
/// callbacks from the IO Thread.
class IOSystem : public NonMovable
{
public:
    IOSystem();
    ~IOSystem();

    /// Starts the dedicated IO Thread and begins accepting read requests.
    ///
    /// Returns InvalidState when called while already initialized. A standalone IOSystem object may be initialized
    /// again after Shutdown() completes.
    [[nodiscard]] ErrorCode Initialize(const IOSystemDesc& desc);

    /// Stops accepting requests, drains queued/running reads, wakes the IO Thread, and joins it.
    ///
    /// Shutdown is a no-op when the system is not initialized. Unconsumed completed results are discarded during final
    /// teardown because the service is leaving its active runtime lifetime.
    void Shutdown() noexcept;

    /// Returns true after Initialize() succeeds and before Shutdown() completes.
    [[nodiscard]] bool IsInitialized() const noexcept;

    /// Schedules an asynchronous binary file read.
    ///
    /// A successful return means the request was accepted. File-not-found and read errors are reported later through the
    /// completed read result rather than through this scheduling result.
    [[nodiscard]] Result<IORequestHandle> ReadBinaryFile(const IOReadRequestDesc& desc);

    /// Returns true when the handle is invalid or the referenced request has completed.
    [[nodiscard]] bool IsComplete(const IORequestHandle& handle) const noexcept;

    /// Blocks until the referenced request completes.
    ///
    /// Waiting on an invalid handle returns immediately. Wait() does not consume the completed read result.
    void Wait(const IORequestHandle& handle) const;

    /// Blocks until every supplied request handle completes.
    ///
    /// Invalid handles are skipped. The span is only used during the call.
    void WaitAll(std::span<const IORequestHandle> handles) const;

    /// Attempts to consume one completed binary read result.
    ///
    /// Returns true and writes outResult when a result was available. Returns false when the completion queue is empty.
    /// Each completed read result is popped at most once. The completion queue is global to this IOSystem; results are
    /// not dispatched back to the request submitter. The thread that calls this function owns handling of the returned
    /// result.
    [[nodiscard]] bool TryPopCompletedRead(IOReadResult& outResult);

private:
    std::unique_ptr<IOSystemImpl> impl_;
};
}
