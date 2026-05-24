#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
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
    class GameThreadSystem;

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

        /// Maximum number of render frames accepted by RenderSystem but not yet completed on the Render Thread.
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
        /// the selected backend. The first implementation also creates the minimal triangle resources used by
        /// RenderFrame().
        [[nodiscard]] ErrorCode CreateMainSwapchain(const RenderSurfaceDesc& desc);

        /// Destroys the main swapchain on the Render Thread if one exists.
        void DestroyMainSwapchain() noexcept;

        /// Renders one first-stage frame to the main swapchain.
        ///
        /// Must be called on the Game Thread after GameThreadSystem binds its Game Thread id to RenderSystem.
        /// The current implementation acquires a render frame slot, submits a render command that clears the back
        /// buffer, draws a simple triangle, submits the command list, and presents. The call blocks only when all
        /// render frame slots are already in flight. Submission or Render Thread execution errors are fatal and
        /// terminate the process.
        void RenderFrame();

        /// Returns the configured render-frame slot count.
        [[nodiscard]] UInt32 GetMaxRenderFramesInFlight() const noexcept;

        /// Returns the number of render frames submitted but not yet completed on the Render Thread.
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

    private:
        friend class GameThreadSystem;

        void BindGameThread(ThreadId gameThreadId) noexcept;
        void ClearGameThreadBinding() noexcept;

        [[nodiscard]] ErrorCode ExecuteSynchronous(std::string debugName, RenderSynchronousFunction function);
        void SubmitFunction(std::string debugName, RenderCommandFunction function);

        std::unique_ptr<RenderSystemImpl> impl_;
    };
} // namespace ve
