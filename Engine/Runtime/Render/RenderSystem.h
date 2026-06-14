#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/FrameRenderer.h"
#include "Engine/Runtime/Render/RenderPass.h"
#include "Engine/Runtime/Render/RenderTarget.h"
#include "Engine/Runtime/Threading/FrameEndSync.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <functional>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace ve
{
    struct RenderSystemImpl;

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
    struct RenderSystemInitParam
    {
        /// Diagnostic name copied into the owned Render Thread.
        std::string threadName = "VEngineRenderThread";

        /// RHI device creation options used by Application after the Render Thread starts.
        RenderDeviceDesc device;
    };

    /// Backend-native handles queried from RenderSystem after RHI initialization.
    struct RenderNativeHandles
    {
        RenderBackend backend = RenderBackend::D3D12;
        bool hasMainSwapchain = false;
        void* device = nullptr;
        void* immediateContext = nullptr;
        void* graphicsQueue = nullptr;
    };

    /// Callable payload executed on the Render Thread.
    using RenderCommandFunction = std::function<void()>;

    /// Callable payload used by RenderSystem for synchronous lifecycle operations on the Render Thread.
    using RenderSynchronousFunction = std::function<ErrorCode()>;

    /// One command submitted to the Render Thread.
    ///
    /// debugName is diagnostic and may be empty. function must be callable as `void()`. Captured references must remain
    /// valid until the command executes; future render-resource handles should be preferred over raw pointers when
    /// crossing from Game Thread or ResourceSystem code to Render Thread code.
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
        /// Returns InvalidState when called while already initialized. A standalone RenderSystem object may be
        /// initialized again after Shutdown() completes.
        [[nodiscard]] ErrorCode Initialize(const RenderSystemInitParam& initParam);

        /// Stops accepting commands, drains accepted work, wakes the Render Thread, and joins it.
        ///
        /// Shutdown is a no-op when the system is not initialized. Accepted commands are executed before the Render
        /// Thread exits. Commands submitted after shutdown starts are rejected by Submit().
        void Shutdown() noexcept;

        /// Returns true after Initialize() succeeds and before Shutdown() completes.
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Returns the last known id of the owned Render Thread.
        [[nodiscard]] ThreadId GetRenderThreadId() const noexcept;

        /// Assigns the Scene-Render frame-end sync primitive used by this RenderSystem.
        void SetSceneThreadRenderThreadFrameEndSync(SceneThreadRenderThreadFrameEndSync* sync) noexcept;

        /// Enqueues one render-thread frame-end fence signal through RenderCommandQueue.
        [[nodiscard]] ErrorCode SubmitFrameEndFenceSignal(UInt32 fenceIndex);

        /// Creates the RHI device on the Render Thread.
        ///
        /// This is an explicit lifecycle API. It internally schedules synchronous render-thread work rather than
        /// requiring callers to submit ad hoc render commands that mutate RHI lifetime.
        [[nodiscard]] ErrorCode InitializeDevice(const RenderDeviceDesc& desc);

        /// Queries backend and native handles used by platform integrations such as Editor UI backends.
        ///
        /// The RHI device must already be initialized. hasMainSwapchain indicates whether the main presentation
        /// surface has been created.
        [[nodiscard]] ErrorCode QueryNativeHandles(RenderNativeHandles& outHandles);

        /// Destroys the RHI device and any main swapchain owned by this RenderSystem on the Render Thread.
        void ShutdownDevice() noexcept;

        /// Returns the backend of the initialized RHI device.
        ///
        /// Calling this before InitializeDevice() succeeds is API misuse.
        [[nodiscard]] RenderBackend GetDeviceBackend() const noexcept;

        /// Creates the main swapchain on the Render Thread.
        ///
        /// The RHI device must already be initialized. The surface descriptor must carry the native handle required by
        /// the selected backend. The first implementation also creates the minimal triangle resources used by
        /// RenderFrame().
        [[nodiscard]] ErrorCode CreateMainSwapchain(const RenderSurfaceDesc& desc);

        /// Destroys the main swapchain on the Render Thread if one exists.
        void DestroyMainSwapchain() noexcept;

        /// Creates the first-stage triangle pass object for a Scene Thread-owned renderer.
        ///
        /// The returned pass references RenderSystem-owned RHI resources, but pass ownership belongs to the caller's
        /// FrameRenderer. This keeps frame orchestration on the Scene Thread while RenderSystem keeps low-level RHI
        /// resource ownership on the Render Thread.
        [[nodiscard]] std::unique_ptr<RenderPass> CreateTriangleForwardPass(
            std::shared_ptr<RTRenderTarget> colorTarget = nullptr);

        /// Initializes a Scene Thread-owned render resource proxy on the Render Thread.
        ///
        /// The description is copied at submission time. This avoids sharing mutable CPU-side RenderTarget state with
        /// the Render Thread while still keeping the RT proxy alive through the captured shared_ptr.
        [[nodiscard]] ErrorCode InitRenderResource(std::shared_ptr<RTRenderTarget> renderTarget,
                                                   RenderTargetDesc desc);

        /// Enqueues one complete main-swapchain frame on the Render Thread.
        ///
        /// The renderer is prepared and owned by Scene Thread code, then captured by shared_ptr so already queued frame
        /// work remains valid even if Scene Thread replaces its current renderer before the Render Thread consumes the
        /// command.
        [[nodiscard]] ErrorCode RenderFrame(std::shared_ptr<FrameRenderer> renderer);

        /// Submits a command to execute on the Render Thread.
        ///
        /// Returns InvalidState before initialization, during shutdown, or after shutdown. Returns InvalidArgument when
        /// the command has no callable function. A successful return means the command was accepted and will run before
        /// a later successful Flush() completes or before Shutdown() returns.
        [[nodiscard]] ErrorCode EnqueueCommand(RenderCommand command);

        /// Blocks until every command accepted before this call has executed on the Render Thread.
        ///
        /// Flush() is a CPU render command queue fence. It does not wait for GPU idle or future RHI queue completion.
        [[nodiscard]] ErrorCode Flush();

    private:
        [[nodiscard]] ErrorCode ExecuteSynchronous(std::string debugName, RenderSynchronousFunction function);
        [[nodiscard]] ErrorCode EnqueueCommand(std::string debugName, RenderCommandFunction function);

        std::unique_ptr<RenderSystemImpl> impl_;
    };
} // namespace ve
