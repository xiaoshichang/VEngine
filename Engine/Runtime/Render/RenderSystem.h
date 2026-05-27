#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/EditorUiFrame.h"
#include "Engine/Runtime/Scene/SceneRenderSnapshot.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <functional>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace ve
{
    struct RenderSystemImpl;
    class GameThreadSystem;
    class ManualResetEvent;
    class ResourceManager;

    constexpr UInt32 DefaultMaxRenderFramesInFlight = 2;

    /// Selects the graphics backend owned by RenderSystem.
    enum class RenderBackend
    {
        D3D11,
        D3D12,
        Metal,
    };

    /// Describes RHI device creation owned by RenderSystem.
    struct RenderDeviceDesc
    {
        /// Backend used for the RHI device.
        RenderBackend backend = RenderBackend::D3D12;

        /// Enables backend debug validation where supported.
        bool enableDebugDevice = false;
    };

    /// Describes the main presentation surface used by Player or first-stage tests.
    struct RenderSurfaceDesc
    {
        /// Native platform window, such as HWND on Windows.
        void* nativeWindow = nullptr;

        /// Native presentation layer, such as CAMetalLayer on Apple platforms.
        void* nativeLayer = nullptr;

        /// Surface width in pixels.
        UInt32 width = 0;

        /// Surface height in pixels.
        UInt32 height = 0;

        /// Preferred color format for the main swapchain.
        rhi::RhiFormat colorFormat = rhi::RhiFormat::Bgra8Unorm;

        /// Preferred back-buffer count.
        UInt32 bufferCount = 2;
    };

    /// Describes the Render Thread created by RenderSystem::Initialize().
    struct RenderSystemDesc
    {
        /// Diagnostic name copied into the owned Render Thread.
        std::string threadName = "VEngineRenderThread";

        /// Number of render frame contexts in the Render Thread ring.
        UInt32 maxFramesInFlight = DefaultMaxRenderFramesInFlight;

        /// RHI device creation options used by Application after the Render Thread starts.
        RenderDeviceDesc device;
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

    /// Callable payload used by RenderSystem for synchronous lifecycle operations on the Render Thread.
    using RenderSynchronousFunction = std::function<ErrorCode(RenderThreadContext&)>;

    /// One command submitted to the Render Thread.
    ///
    /// debugName is diagnostic and may be empty. function must be callable as `void(RenderThreadContext&)`. Captured
    /// references must remain valid until the command executes; future render-resource handles should be preferred over
    /// raw pointers when crossing from Game Thread or ResourceSystem code to Render Thread code.
    struct RenderCommand
    {
        std::string debugName;
        RenderCommandFunction function;
    };

    /// CPU-side fence inserted into the Render Thread command stream.
    ///
    /// BeginFrameEndFence() resets the fence and submits a private Render Thread command that signals it. Waiting on
    /// the fence blocks until the Render Thread has executed every command submitted before that signal command.
    class RenderCommandFence : public NonMovable
    {
    public:
        RenderCommandFence();
        ~RenderCommandFence();

        /// Returns true when this fence has no pending signal command or when the signal command has executed.
        [[nodiscard]] bool IsComplete() const noexcept;

        /// Blocks until this fence is complete.
        void Wait() noexcept;

    private:
        friend class RenderSystem;

        void SetCompletionEvent(std::shared_ptr<ManualResetEvent> completed) noexcept;

        std::shared_ptr<ManualResetEvent> completed_;
    };

    /// Owns the Render Thread and lock-free render command queue.
    ///
    /// RenderSystem is a runtime service managed by EngineRuntime. It owns the long-lived Render Thread, the command
    /// execution boundary, and first-stage RHI device/swapchain lifecycle for Player and Editor entry points.
    class RenderSystem : public NonMovable
    {
    public:
        RenderSystem();
        ~RenderSystem();

        /// Starts the Render Thread and begins accepting render commands.
        ///
        /// Fatal-logs and terminates the process when called while already initialized or when startup cannot complete.
        /// A standalone RenderSystem object may be initialized again after Shutdown() completes.
        void Initialize(const RenderSystemDesc& desc);

        /// Stops accepting commands, drains accepted work, wakes the Render Thread, and joins it.
        ///
        /// Shutdown is a no-op when the system is not initialized. Accepted commands are executed before the Render
        /// Thread exits. Commands submitted after shutdown starts are rejected by Submit().
        void Shutdown() noexcept;

        /// Returns true after Initialize() succeeds and before Shutdown() completes.
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Returns the last known id of the owned Render Thread.
        [[nodiscard]] ThreadId GetRenderThreadId() const noexcept;

        /// Creates the RHI device on the Render Thread.
        ///
        /// This is an explicit lifecycle API. It internally schedules synchronous render-thread work rather than
        /// requiring callers to submit ad hoc render commands that mutate RHI lifetime.
        [[nodiscard]] ErrorCode InitializeDevice(const RenderDeviceDesc& desc);

        /// Destroys the RHI device and any main swapchain owned by this RenderSystem on the Render Thread.
        void ShutdownDevice() noexcept;

        /// Returns true when an RHI device has been created and has not been shut down.
        [[nodiscard]] bool HasDevice() const noexcept;

        /// Returns the backend of the initialized RHI device.
        ///
        /// Calling this before HasDevice() is true is API misuse.
        [[nodiscard]] RenderBackend GetDeviceBackend() const noexcept;

        /// Creates the main swapchain on the Render Thread.
        ///
        /// The RHI device must already be initialized. The surface descriptor must carry the native handle required by
        /// the selected backend. The first implementation also creates the minimal scene pipeline resources used by
        /// SubmitFrame().
        [[nodiscard]] ErrorCode CreateMainSwapchain(const RenderSurfaceDesc& desc);

        /// Destroys the main swapchain on the Render Thread if one exists.
        void DestroyMainSwapchain() noexcept;

        /// Renders one clear-only fallback frame to the main swapchain.
        ///
        /// Must be called on the Game Thread after GameThreadSystem binds its Game Thread id to RenderSystem.
        /// The current implementation advances a render-frame context ring, submits a render command that clears the
        /// back buffer, submits the command list, and presents. Scene rendering should use SubmitFrame(); this fallback
        /// is intentionally non-drawing so startup never presents placeholder geometry. Submission or Render Thread
        /// execution errors are fatal and terminate the process.
        void RenderFrame();

        /// Submits a render-safe scene snapshot produced by the Game Thread.
        ///
        /// The snapshot must not contain live Scene, GameObject, or Component pointers. Invalid state or submission
        /// failures are fatal and terminate the process, matching RenderFrame() and Submit().
        void SubmitFrame(SceneRenderSnapshot snapshot);

        /// Submits one immutable Editor UI frame from the platform/editor main thread and renders it on the Render
        /// Thread. This path is used for Editor-owned UI surfaces such as the Project Launcher; the UI draw data must
        /// already be detached from Dear ImGui before submission.
        void SubmitEditorUiFrame(EditorUiFrameData frameData);

        /// Synchronizes ResourceManager mesh/material resources into the Render Thread resource registry.
        ///
        /// The Game Thread compares ResourceManager revisions against its submitted mirror and enqueues render-resource
        /// add/update/remove commands only for resources whose lifecycle or revision changed.
        void SynchronizeRenderResources(const ResourceManager& resourceManager);

        /// Returns the configured render-frame slot count.
        [[nodiscard]] UInt32 GetMaxRenderFramesInFlight() const noexcept;

        /// Returns zero in the current FrameEndSync-driven CPU pacing model.
        ///
        /// RenderSystem no longer owns a CPU in-flight frame counter. Future diagnostics should expose render-frame
        /// context ring state through a dedicated debug API instead of using this legacy query.
        [[nodiscard]] SizeT GetRenderFramesInFlight() const noexcept;

        /// Submits a command to execute on the Render Thread.
        ///
        /// Must be called on the Game Thread after GameThreadSystem binds its Game Thread id to RenderSystem. Invalid
        /// state, empty command functions, or queue push failures are fatal and terminate the process. A successful
        /// call means the command was accepted and will run before a later successful Flush() completes or before
        /// Shutdown() returns.
        void Submit(RenderCommand command);

        /// Blocks until every command accepted before this call has executed on the Render Thread.
        ///
        /// Flush() is a CPU render command queue fence. It does not wait for GPU idle or future RHI queue completion.
        [[nodiscard]] ErrorCode Flush();

        /// Inserts a CPU fence into the Render Thread command stream.
        ///
        /// Must be called on the Game Thread. The supplied fence becomes pending until the Render Thread executes the
        /// private signal command submitted by this call. GameThreadSystem uses this at EndFrame to control how far the
        /// Game Thread may run ahead of the Render Thread.
        void BeginFrameEndFence(RenderCommandFence& fence);

    private:
        friend class GameThreadSystem;

        void BindGameThread(ThreadId gameThreadId) noexcept;
        void ClearGameThreadBinding() noexcept;

        [[nodiscard]] ErrorCode ExecuteSynchronous(std::string debugName, RenderSynchronousFunction function);
        void SubmitFunction(std::string debugName, RenderCommandFunction function);

        std::unique_ptr<RenderSystemImpl> impl_;
    };
} // namespace ve
