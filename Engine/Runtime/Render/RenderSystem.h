#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <functional>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace ve
{
struct RenderSystemImpl;

/// Describes the Render Thread created by RenderSystem::Initialize().
struct RenderSystemDesc
{
    /// Diagnostic name copied into the owned Render Thread.
    std::string threadName = "VEngineRenderThread";
};

/// Context passed to commands executing on the Render Thread.
///
/// The first version exposes only Render Thread identity. Future RHI integration may add device, frame, upload, or
/// render-resource access here without changing the command submission shape.
class RenderThreadContext
{
public:
    /// Creates a context for the currently running Render Thread.
    explicit RenderThreadContext(ThreadId renderThreadId) noexcept;

    /// Returns the VEngine thread id of the Render Thread executing the current command.
    [[nodiscard]] ThreadId GetRenderThreadId() const noexcept;

private:
    ThreadId renderThreadId_;
};

/// Callable payload executed on the Render Thread.
using RenderCommandFunction = std::function<void(RenderThreadContext&)>;

/// One command submitted to the Render Thread.
///
/// debugName is diagnostic and may be empty. function must be callable as `void(RenderThreadContext&)`. Captured
/// references must remain valid until the command executes; future render-resource handles should be preferred over raw
/// pointers when crossing from Game Thread or ResourceSystem code to Render Thread code.
struct RenderCommand
{
    std::string debugName;
    RenderCommandFunction function;
};

/// Owns the Render Thread and lock-free render command queue.
///
/// RenderSystem is a runtime service managed by EngineRuntime. The first version does not own an RHI device or
/// swapchain; it establishes the long-lived thread and command execution boundary used by later renderer work.
class RenderSystem : public NonMovable
{
public:
    RenderSystem();
    ~RenderSystem();

    /// Starts the Render Thread and begins accepting render commands.
    ///
    /// Returns InvalidState when called while already initialized. A standalone RenderSystem object may be initialized
    /// again after Shutdown() completes.
    [[nodiscard]] Result<void> Initialize(const RenderSystemDesc& desc);

    /// Stops accepting commands, drains accepted work, wakes the Render Thread, and joins it.
    ///
    /// Shutdown is a no-op when the system is not initialized. Accepted commands are executed before the Render Thread
    /// exits. Commands submitted after shutdown starts are rejected by Submit().
    void Shutdown() noexcept;

    /// Returns true after Initialize() succeeds and before Shutdown() completes.
    [[nodiscard]] bool IsInitialized() const noexcept;

    /// Returns the last known id of the owned Render Thread.
    [[nodiscard]] ThreadId GetRenderThreadId() const noexcept;

    /// Submits a command to execute on the Render Thread.
    ///
    /// Returns InvalidState before initialization, during shutdown, or after shutdown. Returns InvalidArgument when the
    /// command has no callable function. A successful return means the command was accepted and will run before a later
    /// successful Flush() completes or before Shutdown() returns.
    [[nodiscard]] Result<void> Submit(RenderCommand command);

    /// Blocks until every command accepted before this call has executed on the Render Thread.
    ///
    /// Flush() is a CPU render command queue fence. It does not wait for GPU idle or future RHI queue completion.
    [[nodiscard]] Result<void> Flush();

private:
    [[nodiscard]] Result<void> SubmitFunction(std::string debugName, RenderCommandFunction function);

    std::unique_ptr<RenderSystemImpl> impl_;
};
}
