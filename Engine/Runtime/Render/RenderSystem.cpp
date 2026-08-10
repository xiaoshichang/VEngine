#include "Engine/Runtime/Render/RenderSystem.h"

#if VE_PLATFORM_WINDOWS && VE_ENABLE_D3D11
#include "Engine/RHI/D3D11/D3D11Rhi.h"
#endif
#if VE_PLATFORM_WINDOWS && VE_ENABLE_D3D12
#include "Engine/RHI/D3D12/D3D12Rhi.h"
#endif
#if VE_ENABLE_METAL
#include "Engine/RHI/Metal/MetalRhi.h"
#endif

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/ScopeExit.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/AutoreleasePool.h"
#include "Engine/Runtime/Render/FrameContext.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"
#include "Engine/Runtime/Render/RHIShaderModuleManager.h"
#include "Engine/Runtime/Render/RenderCommandQueue.h"
#include "Engine/Runtime/Render/RenderFramePipeline.h"
#include "Engine/Runtime/Render/RenderResourceLifetime.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderViewState.h"
#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <array>
#include <exception>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    struct RenderSystemLifetimeFlags
    {
        AtomicBool acceptingCommands{false};
        AtomicBool stopRequested{false};
        AtomicBool initialized{false};
    };

    struct MainSwapchainState
    {
        std::unique_ptr<rhi::RhiSwapchain> swapchain;
        Atomic<UInt64> pendingResizeExtent{0};
        AtomicBool resizeCommandQueued{false};
    };

    struct RenderThreadControlState
    {
        Semaphore commandSemaphore{0};
        RenderCommandQueue commandQueue;
        RenderSystemLifetimeFlags lifetimeFlags;
        AtomicSize activeSubmitCount{0};
    };

    struct RenderRhiState
    {
        Atomic<int> backendValue{-1};
        std::unique_ptr<rhi::RhiDevice> device;
        MainSwapchainState mainSwapchain;
        RHIShaderModuleManager shaderModuleManager;
        RHIPipelineManager pipelineManager;
    };

    struct ActiveFrameRecordingState
    {
        void Begin(UInt32 slotIndex, UInt64 fenceValue) noexcept
        {
            active = true;
            frameSlotIndex = slotIndex;
            submissionFenceValue = fenceValue;
        }

        void Reset() noexcept
        {
            active = false;
            frameSlotIndex = 0;
            submissionFenceValue = 0;
        }

        bool active = false;
        UInt32 frameSlotIndex = 0;
        UInt64 submissionFenceValue = 0;
    };

    struct RenderFrameSchedulingState
    {
        std::array<FrameContext, RenderFrameContextCount> frameContexts;
        ActiveFrameRecordingState recording;
        UInt64 nextFrameIndex = 1;
    };

    struct RenderDiagnosticsState
    {
        RenderPerformanceStatisticsExchange performanceStatistics;
        Atomic<UInt64> recordedDrawCallCount{0};
        FrameGraphDebugCaptureExchange frameGraphDebugCapture;
    };

    struct RenderSystemImpl
    {
        Thread thread;
        Atomic<UInt64> renderThreadIdValue{0};
        // frame sync between scene thread
        SceneThreadRenderThreadFrameEndSync* sceneThreadRenderThreadFrameEndSync = nullptr;

        RenderThreadControlState threadControl;
        RenderRhiState rhi;
        RenderFrameSchedulingState frameScheduling;
        RenderDiagnosticsState diagnostics;
        std::unique_ptr<VirtualShadowManager> virtualShadowManager;
    };

    namespace
    {
        [[noreturn]] void FailRenderSystemFrame(std::string_view context, ErrorCode error, const rhi::RhiDevice* device)
        {
            std::string message(context);
            message += ": ";
            message += ToString(error);
            if (device != nullptr)
            {
                const char* backendError = device->GetLastErrorMessage();
                if (backendError != nullptr && backendError[0] != '\0')
                {
                    message += ". Backend error: ";
                    message += backendError;
                }
            }

            VE_LOG_ERROR_CATEGORY("Render", "{}", message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message.c_str());
            std::terminate();
        }

        void RequireRenderSystemFrameSuccess(ErrorCode result, std::string_view context, const rhi::RhiDevice* device)
        {
            if (result != ErrorCode::None)
            {
                FailRenderSystemFrame(context, result, device);
            }
        }

        [[nodiscard]] const char* ToString(RenderBackend backend) noexcept
        {
            switch (backend)
            {
            case RenderBackend::D3D11:
                return "D3D11";
            case RenderBackend::D3D12:
                return "D3D12";
            case RenderBackend::Metal:
                return "Metal";
            }

            return "Unknown";
        }

        [[nodiscard]] ErrorCode ValidateSurfaceDesc(const RenderSurfaceDesc& desc)
        {
            if (desc.width == 0 || desc.height == 0)
            {
                return ErrorCode::InvalidArgument;
            }

            if (desc.bufferCount == 0)
            {
                return ErrorCode::InvalidArgument;
            }

            if (desc.nativeWindow == nullptr && desc.nativeLayer == nullptr)
            {
                return ErrorCode::InvalidArgument;
            }

            return ErrorCode::None;
        }

        [[nodiscard]] constexpr UInt64 PackExtent(rhi::RhiExtent2D extent) noexcept
        {
            return (static_cast<UInt64>(extent.width) << 32u) | static_cast<UInt64>(extent.height);
        }

        [[nodiscard]] constexpr rhi::RhiExtent2D UnpackExtent(UInt64 packedExtent) noexcept
        {
            return rhi::RhiExtent2D{
                static_cast<UInt32>(packedExtent >> 32u),
                static_cast<UInt32>(packedExtent & 0xffffffffu),
            };
        }

        [[nodiscard]] rhi::RhiSwapchainDesc ToRhiSwapchainDesc(const RenderSurfaceDesc& desc)
        {
            rhi::RhiSwapchainDesc rhiDesc = {};
            rhiDesc.nativeWindow = desc.nativeWindow;
            rhiDesc.nativeLayer = desc.nativeLayer;
            rhiDesc.width = desc.width;
            rhiDesc.height = desc.height;
            rhiDesc.colorFormat = desc.colorFormat;
            rhiDesc.bufferCount = desc.bufferCount;
            rhiDesc.debugName = "VEngineMainSwapchain";
            return rhiDesc;
        }

        [[nodiscard]] std::unique_ptr<rhi::RhiDevice> CreateRhiDevice(const RenderDeviceDesc& desc)
        {
            switch (desc.backend)
            {
            case RenderBackend::D3D11:
#if VE_PLATFORM_WINDOWS && VE_ENABLE_D3D11
                return rhi::CreateD3D11Device(desc.enableDebugDevice);
#else
                return nullptr;
#endif

            case RenderBackend::D3D12:
#if VE_PLATFORM_WINDOWS && VE_ENABLE_D3D12
                return rhi::CreateD3D12Device(desc.enableDebugDevice);
#else
                return nullptr;
#endif

            case RenderBackend::Metal:
#if VE_ENABLE_METAL
                return rhi::CreateMetalDevice(desc.enableDebugDevice);
#else
                return nullptr;
#endif
            }

            return nullptr;
        }

        void RetireRhiObjects(RenderSystemImpl& impl, RhiObjectList objects)
        {
            VE_ASSERT_RENDER_THREAD();
            if (objects.empty())
            {
                return;
            }

            std::array<UInt64, RenderFrameContextCount> dependencies{};
            for (UInt32 frameSlotIndex = 0; frameSlotIndex < RenderFrameContextCount; ++frameSlotIndex)
            {
                dependencies[frameSlotIndex] = impl.frameScheduling.frameContexts[frameSlotIndex].GetSubmittedFenceValue();
            }
            if (impl.frameScheduling.recording.active)
            {
                VE_ASSERT(impl.frameScheduling.recording.frameSlotIndex < RenderFrameContextCount);
                dependencies[impl.frameScheduling.recording.frameSlotIndex] =
                    std::max(dependencies[impl.frameScheduling.recording.frameSlotIndex], impl.frameScheduling.recording.submissionFenceValue);
            }

            UInt32 dependencyCount = 0;
            for (UInt64 fenceValue : dependencies)
            {
                dependencyCount += fenceValue != 0 ? 1u : 0u;
            }
            if (dependencyCount == 0)
            {
                return;
            }

            auto batch = std::make_shared<PendingDeleteRTResourceBatch>();
            batch->resources = std::move(objects);
            batch->remainingFenceCount = dependencyCount;
            for (UInt32 frameSlotIndex = 0; frameSlotIndex < RenderFrameContextCount; ++frameSlotIndex)
            {
                const UInt64 fenceValue = dependencies[frameSlotIndex];
                if (fenceValue == 0)
                {
                    continue;
                }

                impl.frameScheduling.frameContexts[frameSlotIndex].EnqueuePendingDeleteResource(fenceValue, batch);
            }
        }

        void ClearRetiredRhiObjectsAfterWaitIdle(RenderSystemImpl& impl) noexcept
        {
            VE_ASSERT_RENDER_THREAD();
            for (FrameContext& frameContext : impl.frameScheduling.frameContexts)
            {
                frameContext.ClearRetiredRhiObjectsAfterWaitIdle();
            }
            impl.frameScheduling.recording.Reset();
        }

        void DestroyFrameResources(RenderSystemImpl& impl)
        {
            ClearRetiredRhiObjectsAfterWaitIdle(impl);
            for (FrameContext& frameContext : impl.frameScheduling.frameContexts)
            {
                const bool shutdown = frameContext.Shutdown();
                VE_ASSERT_MESSAGE(shutdown, "Failed to shut down a render frame context.");
            }
        }

        [[nodiscard]] bool WaitForAllFrameContexts(RenderSystemImpl& impl)
        {
            for (UInt32 frameSlotIndex = 0; frameSlotIndex < RenderFrameContextCount; ++frameSlotIndex)
            {
                FrameContext& frameContext = impl.frameScheduling.frameContexts[frameSlotIndex];
                if (frameContext.IsInitialized() && !frameContext.WaitAndReset())
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] ErrorCode CreateFrameResources(RenderSystemImpl& impl)
        {
            VE_ASSERT_MESSAGE(impl.rhi.device != nullptr, "CreateFrameResources requires an initialized RHI device.");

            for (UInt32 contextIndex = 0; contextIndex < RenderFrameContextCount; ++contextIndex)
            {
                if (!impl.frameScheduling.frameContexts[contextIndex].Initialize(*impl.rhi.device, contextIndex))
                {
                    DestroyFrameResources(impl);
                    return ErrorCode::PlatformError;
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode
        PrepareMainSwapchainFrame(RenderSystemImpl& impl, FrameRenderPipelineData& frameData, std::optional<RenderPerformanceStatistics>& completedStatistics)
        {
            VE_ASSERT_RENDER_THREAD();
            VE_ASSERT(impl.rhi.device != nullptr);
            VE_ASSERT(impl.rhi.mainSwapchain.swapchain != nullptr);
            completedStatistics.reset();

            // A FrameContext is a reusable in-flight GPU slot. Waiting here both makes its command resources reusable
            // and releases the pipeline and render proxies retained by its previous submission.
            const UInt64 frameIndex = impl.frameScheduling.nextFrameIndex++;
            const UInt32 frameSlotIndex = static_cast<UInt32>(frameIndex % RenderFrameContextCount);
            FrameContext& frameContext = impl.frameScheduling.frameContexts[frameSlotIndex];
            if (!frameContext.WaitForFrameStartAndReset(*impl.rhi.mainSwapchain.swapchain))
            {
                return ErrorCode::PlatformError;
            }
            const UInt64 completedFrameIndex = frameContext.TakeSubmittedFrameIndex();
            if (completedFrameIndex != 0)
            {
                // Map and release the completed readback before RenderFrame prepares this modulo slot again.
                // Publication remains deferred until the current frame reveals which scene is active.
                if (impl.virtualShadowManager == nullptr)
                {
                    FailRenderSystemFrame("RenderSystem completed a VSM frame without a VSM manager", ErrorCode::InvalidState, impl.rhi.device.get());
                }
                completedStatistics = impl.virtualShadowManager->ConsumeCompletedFrameStatistics(*impl.rhi.device, completedFrameIndex);
                if (!completedStatistics.has_value())
                {
                    FailRenderSystemFrame(
                        "RenderSystem completed a frame without its submitted VSM statistics", ErrorCode::InvalidState, impl.rhi.device.get());
                }
            }

            frameData.frameIndex = frameIndex;
            frameData.frameSlotIndex = frameSlotIndex;
            frameData.device = impl.rhi.device.get();
            frameData.mainSwapchain = impl.rhi.mainSwapchain.swapchain.get();
            frameData.pipelineManager = &impl.rhi.pipelineManager;
            frameData.frameContext = &frameContext;
            frameData.transientResourcePool = &frameContext.GetTransientResourcePool();
            frameData.virtualShadowManager = impl.virtualShadowManager.get();
            return ErrorCode::None;
        }

        struct MainSwapchainSubmitContext
        {
            rhi::RhiDevice* device = nullptr;
            FrameContext* frameContext = nullptr;
        };

        [[nodiscard]] bool SubmitMainSwapchainFrameCallback(void* context, UInt64 submissionFenceValue) noexcept
        {
            auto& submitContext = *static_cast<MainSwapchainSubmitContext*>(context);
            try
            {
                const bool submitted = submitContext.device->Submit(
                    submitContext.frameContext->GetCommandList(), &submitContext.frameContext->GetCompletionFence(), submissionFenceValue);
                if (!submitted)
                {
                    FailRenderSystemFrame("RenderSystem failed to submit the frame", ErrorCode::PlatformError, submitContext.device);
                }
                return true;
            }
            catch (...)
            {
                FailRenderSystemFrame("Unhandled exception while submitting the frame", ErrorCode::Unknown, submitContext.device);
            }
        }

        [[nodiscard]] ErrorCode SubmitMainSwapchainFrame(RenderSystemImpl& impl,
                                                         const FrameRenderPipelineData& frameData,
                                                         const std::shared_ptr<FrameRenderPipeline>& framePipeline) noexcept
        {
            VE_ASSERT_RENDER_THREAD();
            VE_ASSERT(impl.rhi.device != nullptr);
            VE_ASSERT(frameData.frameContext != nullptr);
            VE_ASSERT(framePipeline != nullptr);

            FrameContext& frameContext = *frameData.frameContext;
            MainSwapchainSubmitContext submitContext{impl.rhi.device.get(), &frameContext};
            return frameContext.Submit(&SubmitMainSwapchainFrameCallback, &submitContext);
        }

        [[nodiscard]] ErrorCode PresentMainSwapchainFrame(RenderSystemImpl& impl)
        {
            VE_ASSERT_RENDER_THREAD();
            VE_ASSERT(impl.rhi.mainSwapchain.swapchain != nullptr);
            return impl.rhi.mainSwapchain.swapchain->Present() ? ErrorCode::None : ErrorCode::PlatformError;
        }

        void RetireFrameGraphDebugDataOnRenderThread(RenderSystemImpl& impl, std::shared_ptr<const FrameGraphDebugData> data)
        {
            VE_ASSERT_RENDER_THREAD();
            if (data == nullptr)
            {
                return;
            }

            std::vector<std::shared_ptr<FrameGraphDebugPreviewTexture>> previewTextures = CollectFrameGraphDebugPreviewTextures(*data);
            RhiObjectList retiredObjects;
            for (const std::shared_ptr<FrameGraphDebugPreviewTexture>& previewTexture : previewTextures)
            {
                RhiObjectList previewObjects = previewTexture->TakeRhiObjects();
                retiredObjects.insert(retiredObjects.end(), std::make_move_iterator(previewObjects.begin()), std::make_move_iterator(previewObjects.end()));
            }
            RetireRhiObjects(impl, std::move(retiredObjects));

            // Both the immutable snapshot and the temporary owner list are released on the Render Thread. Every
            // preview owner is empty after Reset, so its destructor cannot release a live RHI object on another thread.
            data.reset();
        }

        void RenderMainSwapchainFrame(RenderSystemImpl& impl, const std::shared_ptr<FrameRenderPipeline>& framePipeline)
        {
            VE_ASSERT_RENDER_THREAD();
            VE_ASSERT(framePipeline != nullptr);

            std::optional<FrameGraphDebugCaptureRequest> debugRequest = impl.diagnostics.frameGraphDebugCapture.ConsumeRequest();
            FrameGraphDebugFrameCapture debugCapture;
            if (debugRequest.has_value())
            {
                debugCapture.request = *debugRequest;
            }

            // Phase 1: acquire a reusable GPU frame slot and assemble the Render Thread execution context.
            FrameRenderPipelineData frameData = {};
            std::optional<RenderPerformanceStatistics> completedStatistics;
            const ErrorCode prepareResult = PrepareMainSwapchainFrame(impl, frameData, completedStatistics);
            RequireRenderSystemFrameSuccess(prepareResult, "RenderSystem failed to prepare the frame", impl.rhi.device.get());
            if (debugRequest.has_value())
            {
                frameData.frameGraphDebugCapture = &debugCapture;
            }
            frameData.builtInShaderResources = framePipeline->GetBuiltInShaderResources();
            // Phase 2: let the product-specific pipeline record scene, overlay, and copy work into the frame command list.
            impl.frameScheduling.recording.Begin(frameData.frameSlotIndex, frameData.frameContext->GetNextSubmissionFenceValue());
            framePipeline->RenderFrame(frameData);
            impl.diagnostics.recordedDrawCallCount.store(frameData.GetCommandList().GetRecordedDrawCallCount(), std::memory_order_release);
            const UInt64 statisticsSceneIdentity =
                frameData.virtualShadowManager != nullptr ? frameData.virtualShadowManager->GetRecordingSceneIdentity(frameData.frameIndex) : 0;
            impl.diagnostics.performanceStatistics.ActivateScene(statisticsSceneIdentity, frameData.frameIndex);
            if (completedStatistics.has_value())
            {
                impl.diagnostics.performanceStatistics.Publish(*completedStatistics);
            }
            // Phase 3: submit the recorded work and bind its object lifetime to the selected FrameContext fence.
            const ErrorCode submitResult = SubmitMainSwapchainFrame(impl, frameData, framePipeline);
            RequireRenderSystemFrameSuccess(submitResult, "RenderSystem failed to submit the frame", impl.rhi.device.get());
            impl.frameScheduling.recording.Reset();
            if (debugRequest.has_value())
            {
                FrameGraphDebugCapturePublishResult completion = CompleteFrameGraphDebugCapture(
                    impl.diagnostics.frameGraphDebugCapture, submitResult, *debugRequest, std::move(debugCapture.data), std::move(debugCapture.failureMessage));
                RetireFrameGraphDebugDataOnRenderThread(impl, std::move(completion.dataToRetire));
            }
            if (frameData.virtualShadowManager != nullptr && statisticsSceneIdentity != 0)
            {
                frameData.frameContext->SetSubmittedFrameIndex(frameData.frameIndex);
                frameData.virtualShadowManager->NotifyFrameSubmitted(
                    frameData.frameIndex, frameData.frameContext->GetCompletionFence(), frameData.frameContext->GetSubmittedFenceValue());
            }

            // Phase 4: presentation happens after a successful queue submission; the FrameContext now owns all data
            // that must remain alive even if Present reports a surface or device error.
            const ErrorCode presentResult = PresentMainSwapchainFrame(impl);
            RequireRenderSystemFrameSuccess(presentResult, "RenderSystem failed to present the frame", impl.rhi.device.get());
        }

        void ExecuteCommand(RenderCommand& command) noexcept
        {
            VE_ASSERT_RENDER_THREAD();
            PlatformAutoreleasePool autoreleasePool;
            try
            {
                command.function();
            }
            catch (...)
            {
                FailRenderSystemFrame("Unhandled exception escaped a RenderSystem command", ErrorCode::Unknown, nullptr);
            }
        }

        void RenderThreadLoop(RenderSystemImpl& impl)
        {
            const ThreadId renderThreadId = GetCurrentThreadId();
            impl.renderThreadIdValue.store(renderThreadId.value, std::memory_order_release);
            SetExpectedRenderThreadId(renderThreadId);

            for (;;)
            {
                while (std::optional<RenderCommand> command = impl.threadControl.commandQueue.TryPop())
                {
                    ExecuteCommand(*command);
                }

                if (impl.threadControl.lifetimeFlags.stopRequested.load(std::memory_order_acquire))
                {
                    if (!impl.threadControl.commandQueue.IsEmptyForConsumer())
                    {
                        continue;
                    }

                    break;
                }

                impl.threadControl.commandSemaphore.Acquire();
            }

            while (std::optional<RenderCommand> command = impl.threadControl.commandQueue.TryPop())
            {
                ExecuteCommand(*command);
            }

            SetExpectedRenderThreadId(ThreadId{});
        }

        void DestroyRhiStateOnRenderThread(RenderSystemImpl& impl)
        {
            VE_ASSERT_RENDER_THREAD();

            if (impl.rhi.device != nullptr)
            {
                impl.rhi.device->WaitIdle();
                ClearRetiredRhiObjectsAfterWaitIdle(impl);
            }

            RetireFrameGraphDebugDataOnRenderThread(impl, impl.diagnostics.frameGraphDebugCapture.Reset());
            impl.diagnostics.performanceStatistics.Reset();
            impl.rhi.mainSwapchain.pendingResizeExtent.store(0, std::memory_order_release);
            impl.rhi.mainSwapchain.resizeCommandQueued.store(false, std::memory_order_release);
            impl.virtualShadowManager.reset();
            impl.rhi.pipelineManager.Clear();
            impl.rhi.shaderModuleManager.Clear();
            DestroyFrameResources(impl);
            impl.rhi.mainSwapchain.swapchain.reset();
            if (impl.rhi.device != nullptr)
            {
                impl.rhi.device.reset();
            }

            impl.rhi.backendValue.store(-1, std::memory_order_release);
        }

        void EnqueueInternalCommand(RenderSystemImpl& impl, RenderCommand command) noexcept
        {
            ErrorCode pushResult = impl.threadControl.commandQueue.Push(std::move(command));
            VE_ASSERT_MESSAGE(pushResult == ErrorCode::None, "RenderSystem failed to enqueue an internal render command.");
            impl.threadControl.commandSemaphore.Release();
        }

        void QueueMainSwapchainResizeCommand(RenderSystemImpl& impl);

        void ProcessMainSwapchainResize(RenderSystemImpl& impl)
        {
            VE_ASSERT_RENDER_THREAD();

            UInt64 packedExtent = impl.rhi.mainSwapchain.pendingResizeExtent.exchange(0, std::memory_order_acq_rel);
            if (packedExtent != 0 && impl.rhi.mainSwapchain.swapchain != nullptr)
            {
                rhi::RhiExtent2D requestedExtent = UnpackExtent(packedExtent);
                const rhi::RhiExtent2D currentExtent = impl.rhi.mainSwapchain.swapchain->GetExtent();
                if (requestedExtent.width != currentExtent.width || requestedExtent.height != currentExtent.height)
                {
                    if (impl.rhi.device != nullptr)
                    {
                        // Frame completion fences are queued before Present. Waiting for the device's internal fence
                        // here also covers presentation work that may still reference the old back buffers.
                        impl.rhi.device->WaitIdle();
                        ClearRetiredRhiObjectsAfterWaitIdle(impl);
                    }

                    if (!WaitForAllFrameContexts(impl))
                    {
                        VE_LOG_ERROR_CATEGORY("Render", "Failed to wait for in-flight frames before resizing the main swapchain.");
                    }
                    else
                    {
                        const UInt64 newerPackedExtent = impl.rhi.mainSwapchain.pendingResizeExtent.exchange(0, std::memory_order_acq_rel);
                        if (newerPackedExtent != 0)
                        {
                            requestedExtent = UnpackExtent(newerPackedExtent);
                        }

                        const rhi::RhiExtent2D resizedFromExtent = impl.rhi.mainSwapchain.swapchain->GetExtent();
                        if ((requestedExtent.width != resizedFromExtent.width || requestedExtent.height != resizedFromExtent.height) &&
                            !impl.rhi.mainSwapchain.swapchain->Resize(requestedExtent))
                        {
                            const char* backendError = impl.rhi.device != nullptr ? impl.rhi.device->GetLastErrorMessage() : nullptr;
                            VE_LOG_ERROR_CATEGORY("Render",
                                                  "Failed to resize the main swapchain to {}x{}. Backend error: {}",
                                                  requestedExtent.width,
                                                  requestedExtent.height,
                                                  backendError != nullptr && backendError[0] != '\0' ? backendError : "Unknown");
                        }
                    }
                }
            }

            impl.rhi.mainSwapchain.resizeCommandQueued.store(false, std::memory_order_release);
            if (impl.rhi.mainSwapchain.pendingResizeExtent.load(std::memory_order_acquire) == 0 ||
                !impl.threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire))
            {
                return;
            }

            bool expected = false;
            if (impl.rhi.mainSwapchain.resizeCommandQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                QueueMainSwapchainResizeCommand(impl);
            }
        }

        void QueueMainSwapchainResizeCommand(RenderSystemImpl& impl)
        {
            EnqueueInternalCommand(impl, RenderCommand{"RenderSystemResizeMainSwapchain", [&impl]() { ProcessMainSwapchainResize(impl); }});
        }

        void StopAndJoinRenderThread(RenderSystemImpl& impl) noexcept
        {
            impl.threadControl.lifetimeFlags.acceptingCommands.store(false, std::memory_order_release);

            while (impl.threadControl.activeSubmitCount.load(std::memory_order_acquire) != 0)
            {
                YieldThread();
            }

            auto rhiDestroyed = std::make_shared<ManualResetEvent>(false);
            EnqueueInternalCommand(impl,
                                   RenderCommand{"RenderSystemDestroyRhiState",
                                                 [&impl, rhiDestroyed]()
                                                 {
                                                     DestroyRhiStateOnRenderThread(impl);
                                                     rhiDestroyed->Set();
                                                 }});
            rhiDestroyed->Wait();

            impl.threadControl.lifetimeFlags.stopRequested.store(true, std::memory_order_release);
            impl.threadControl.commandSemaphore.Release();

            if (impl.thread.IsJoinable())
            {
                const bool joined = impl.thread.Join();
                VE_ASSERT_MESSAGE(joined, "RenderSystem failed to join its Render Thread during shutdown.");
            }

            impl.threadControl.commandQueue.ClearForConsumer();
            impl.renderThreadIdValue.store(0, std::memory_order_release);
            impl.threadControl.lifetimeFlags.stopRequested.store(false, std::memory_order_release);
            impl.threadControl.lifetimeFlags.initialized.store(false, std::memory_order_release);
        }
    } // namespace

    RenderSystem::RenderSystem()
        : impl_(std::make_unique<RenderSystemImpl>())
    {
    }

    RenderSystem::~RenderSystem()
    {
        Shutdown();
    }

    ErrorCode RenderSystem::Initialize(const RenderSystemInitParam& initParam)
    {
        if (impl_->threadControl.lifetimeFlags.initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        impl_->threadControl.lifetimeFlags.stopRequested.store(false, std::memory_order_release);
        impl_->threadControl.lifetimeFlags.acceptingCommands.store(true, std::memory_order_release);
        if (impl_->sceneThreadRenderThreadFrameEndSync != nullptr)
        {
            impl_->sceneThreadRenderThreadFrameEndSync->Reset();
        }

        ErrorCode startResult = impl_->thread.Start(initParam.threadName.empty() ? ThreadDesc{"VEngineRenderThread"} : ThreadDesc{initParam.threadName},
                                                    [this]() { RenderThreadLoop(*impl_); });

        if (startResult != ErrorCode::None)
        {
            impl_->threadControl.lifetimeFlags.acceptingCommands.store(false, std::memory_order_release);
            impl_->threadControl.lifetimeFlags.stopRequested.store(false, std::memory_order_release);
            impl_->threadControl.commandQueue.ClearForConsumer();
            return startResult;
        }

        impl_->threadControl.lifetimeFlags.initialized.store(true, std::memory_order_release);
        return ErrorCode::None;
    }

    void RenderSystem::Shutdown() noexcept
    {
        if (!impl_->threadControl.lifetimeFlags.initialized.load(std::memory_order_acquire))
        {
            return;
        }

        StopAndJoinRenderThread(*impl_);
    }

    bool RenderSystem::IsInitialized() const noexcept
    {
        return impl_->threadControl.lifetimeFlags.initialized.load(std::memory_order_acquire);
    }

    ThreadId RenderSystem::GetRenderThreadId() const noexcept
    {
        return ThreadId{impl_->renderThreadIdValue.load(std::memory_order_acquire)};
    }

    void RenderSystem::SetSceneThreadRenderThreadFrameEndSync(SceneThreadRenderThreadFrameEndSync* sync) noexcept
    {
        VE_ASSERT_MESSAGE(!impl_->threadControl.lifetimeFlags.initialized.load(std::memory_order_acquire),
                          "SetSceneThreadRenderThreadFrameEndSync requires RenderSystem to be stopped.");
        impl_->sceneThreadRenderThreadFrameEndSync = sync;
    }

    void RenderSystem::SubmitFrameEndFenceSignal(UInt32 fenceIndex)
    {
        VE_ASSERT_SCENE_THREAD();
        EnqueueCommand("RenderSystemFrameEndFenceSignal",
                       [sync = impl_->sceneThreadRenderThreadFrameEndSync, fenceIndex]() { sync->NotifyRenderThreadFrameEnd(fenceIndex); });
    }

    ErrorCode RenderSystem::InitializeDevice(const RenderDeviceDesc& desc)
    {
        return ExecuteSynchronous("RenderSystemInitializeDevice",
                                  [this, desc]()
                                  {
                                      if (impl_->rhi.device != nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      std::unique_ptr<rhi::RhiDevice> device = CreateRhiDevice(desc);
                                      if (device == nullptr)
                                      {
                                          return ErrorCode::Unsupported;
                                      }

                                      impl_->rhi.device = std::move(device);
                                      impl_->virtualShadowManager = std::make_unique<VirtualShadowManager>();
                                      impl_->rhi.backendValue.store(static_cast<int>(desc.backend), std::memory_order_release);
                                      VE_LOG_INFO("RenderSystem initialized RHI backend: {}", ToString(desc.backend));
                                      return ErrorCode::None;
                                  });
    }

    ErrorCode RenderSystem::QueryNativeHandles(RenderNativeHandles& outHandles)
    {
        return ExecuteSynchronous("RenderSystemQueryNativeHandles",
                                  [this, &outHandles]()
                                  {
                                      if (impl_->rhi.device == nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      outHandles.backend = static_cast<RenderBackend>(impl_->rhi.backendValue.load(std::memory_order_acquire));
                                      outHandles.hasMainSwapchain = impl_->rhi.mainSwapchain.swapchain != nullptr;
                                      outHandles.device = impl_->rhi.device->GetNativeDeviceHandle();
                                      outHandles.immediateContext = impl_->rhi.device->GetNativeImmediateContextHandle();
                                      outHandles.graphicsQueue = impl_->rhi.device->GetNativeGraphicsQueueHandle();
                                      outHandles.shaderResourceDescriptorAllocator = impl_->rhi.device->GetNativeShaderResourceDescriptorAllocator();
                                      if (impl_->rhi.mainSwapchain.swapchain != nullptr)
                                      {
                                          outHandles.mainSwapchainBufferCount = impl_->rhi.mainSwapchain.swapchain->GetBufferCount();
                                          outHandles.mainSwapchainColorFormat = impl_->rhi.mainSwapchain.swapchain->GetColorFormat();
                                      }
                                      return ErrorCode::None;
                                  });
    }

    void RenderSystem::ShutdownDevice() noexcept
    {
        if (!impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        ErrorCode result = ExecuteSynchronous("RenderSystemShutdownDevice",
                                              [this]()
                                              {
                                                  DestroyRhiStateOnRenderThread(*impl_);
                                                  return ErrorCode::None;
                                              });

        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to shut down its RHI device.");
    }

    RenderBackend RenderSystem::GetDeviceBackend() const noexcept
    {
        const int backendValue = impl_->rhi.backendValue.load(std::memory_order_acquire);
        VE_ASSERT_MESSAGE(backendValue >= 0, "RenderSystem::GetDeviceBackend requires an initialized RHI device.");
        return static_cast<RenderBackend>(backendValue);
    }

    RenderPerformanceStatistics RenderSystem::GetPerformanceStatistics() const
    {
        return impl_->diagnostics.performanceStatistics.GetLatest();
    }

    UInt64 RenderSystem::GetRecordedDrawCallCount() const noexcept
    {
        if (impl_ == nullptr || !impl_->threadControl.lifetimeFlags.initialized.load(std::memory_order_acquire))
        {
            return 0;
        }

        return impl_->diagnostics.recordedDrawCallCount.load(std::memory_order_acquire);
    }

    ErrorCode RenderSystem::RequestFrameGraphDebugCapture(Float32 previewScale)
    {
        VE_ASSERT_SCENE_THREAD();
        impl_->threadControl.activeSubmitCount.fetch_add(1, std::memory_order_acq_rel);
        auto submitCounterGuard = MakeScopeExit([this]() { impl_->threadControl.activeSubmitCount.fetch_sub(1, std::memory_order_acq_rel); });

        if (!impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }
        return impl_->diagnostics.frameGraphDebugCapture.RequestCapture(previewScale);
    }

    FrameGraphDebugCaptureStatus RenderSystem::GetFrameGraphDebugCaptureStatus() const
    {
        return impl_->diagnostics.frameGraphDebugCapture.GetStatus();
    }

    std::string RenderSystem::GetFrameGraphDebugCaptureFailure() const
    {
        return impl_->diagnostics.frameGraphDebugCapture.GetFailureMessage();
    }

    std::shared_ptr<const FrameGraphDebugData> RenderSystem::TakeFrameGraphDebugData()
    {
        VE_ASSERT_SCENE_THREAD();
        return impl_->diagnostics.frameGraphDebugCapture.TakePublishedData();
    }

    std::shared_ptr<const FrameGraphDebugData> RenderSystem::RetireFrameGraphDebugData(std::shared_ptr<const FrameGraphDebugData> data)
    {
        VE_ASSERT_SCENE_THREAD();
        if (data == nullptr)
        {
            return nullptr;
        }

        impl_->threadControl.activeSubmitCount.fetch_add(1, std::memory_order_acq_rel);
        auto submitCounterGuard = MakeScopeExit([this]() { impl_->threadControl.activeSubmitCount.fetch_sub(1, std::memory_order_acq_rel); });

        if (!impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire))
        {
            return data;
        }

        ErrorCode pushResult = ErrorCode::Unknown;
        try
        {
            RenderCommand retireCommand{
                "RenderSystemRetireFrameGraphDebugData",
                [this, queuedData = data]() mutable { RetireFrameGraphDebugDataOnRenderThread(*impl_, std::move(queuedData)); },
            };
            pushResult = impl_->threadControl.commandQueue.Push(std::move(retireCommand));
        }
        catch (...)
        {
            return data;
        }

        if (pushResult != ErrorCode::None)
        {
            return data;
        }

        impl_->threadControl.commandSemaphore.Release();
        return nullptr;
    }

    ErrorCode RenderSystem::CreateMainSwapchain(const RenderSurfaceDesc& desc)
    {
        ErrorCode validateResult = ValidateSurfaceDesc(desc);
        if (validateResult != ErrorCode::None)
        {
            return validateResult;
        }

        return ExecuteSynchronous("RenderSystemCreateMainSwapchain",
                                  [this, desc]()
                                  {
                                      if (impl_->rhi.device == nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      if (impl_->rhi.mainSwapchain.swapchain != nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      std::unique_ptr<rhi::RhiSwapchain> swapchain = impl_->rhi.device->CreateSwapchain(ToRhiSwapchainDesc(desc));
                                      if (swapchain == nullptr)
                                      {
                                          return ErrorCode::PlatformError;
                                      }

                                      impl_->rhi.mainSwapchain.swapchain = std::move(swapchain);
                                      ErrorCode frameResourceResult = CreateFrameResources(*impl_);
                                      if (frameResourceResult != ErrorCode::None)
                                      {
                                          impl_->rhi.mainSwapchain.swapchain.reset();
                                          return frameResourceResult;
                                      }

                                      return ErrorCode::None;
                                  });
    }

    void RenderSystem::DestroyMainSwapchain() noexcept
    {
        if (!impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        ErrorCode result = ExecuteSynchronous("RenderSystemDestroyMainSwapchain",
                                              [this]()
                                              {
                                                  if (impl_->rhi.device != nullptr)
                                                  {
                                                      impl_->rhi.device->WaitIdle();
                                                      ClearRetiredRhiObjectsAfterWaitIdle(*impl_);
                                                  }

                                                  DestroyFrameResources(*impl_);
                                                  impl_->rhi.mainSwapchain.swapchain.reset();
                                                  return ErrorCode::None;
                                              });

        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to destroy its main swapchain.");
    }

    void RenderSystem::RequestMainSwapchainResize(rhi::RhiExtent2D extent)
    {
        VE_ASSERT_SCENE_THREAD();
        if (extent.width == 0 || extent.height == 0 || !impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        impl_->rhi.mainSwapchain.pendingResizeExtent.store(PackExtent(extent), std::memory_order_release);
        bool expected = false;
        if (impl_->rhi.mainSwapchain.resizeCommandQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            QueueMainSwapchainResizeCommand(*impl_);
        }
    }

    void RenderSystem::InitRenderResource(std::shared_ptr<RTRenderTexture> renderTexture, RenderTextureDesc desc)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(renderTexture != nullptr, "RenderSystem::InitRenderResource requires a render texture.");

        EnqueueCommand("RenderSystemInitRenderResource",
                       [this, renderTexture = std::move(renderTexture), desc = std::move(desc)]() mutable
                       {
                           VE_ASSERT(impl_->rhi.device != nullptr);
                           RhiObjectList retiredObjects;
                           if (!renderTexture->MatchesDesc(desc))
                           {
                               retiredObjects = renderTexture->TakeRhiObjects();
                           }
                           renderTexture->InitRenderResource(*impl_->rhi.device, std::move(desc));
                           RetireRhiObjects(*impl_, std::move(retiredObjects));
                       });
    }

    void RenderSystem::InitRenderResource(std::shared_ptr<RTMeshResource> meshResource, RTMeshResourceDesc desc)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(meshResource != nullptr, "RenderSystem::InitRenderResource requires a mesh resource.");

        EnqueueCommand("RenderSystemInitMeshResource",
                       [this, meshResource = std::move(meshResource), desc = std::move(desc)]() mutable
                       {
                           VE_ASSERT(impl_->rhi.device != nullptr);
                           RhiObjectList retiredObjects = meshResource->TakeRhiObjects();
                           meshResource->InitRenderResource(*impl_->rhi.device, std::move(desc));
                           RetireRhiObjects(*impl_, std::move(retiredObjects));
                       });
    }

    void RenderSystem::InitRenderResource(std::shared_ptr<RTShaderResource> shaderResource, RTShaderResourceDesc desc)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(shaderResource != nullptr, "RenderSystem::InitRenderResource requires a shader resource.");

        EnqueueCommand("RenderSystemInitShaderResource",
                       [this, shaderResource = std::move(shaderResource), desc = std::move(desc)]() mutable
                       {
                           VE_ASSERT(impl_->rhi.device != nullptr);
                           RhiObjectList retiredObjects = shaderResource->TakeRhiObjects();
                           shaderResource->InitRenderResource(*impl_->rhi.device, std::move(desc));
                           RetireRhiObjects(*impl_, std::move(retiredObjects));
                       });
    }

    void RenderSystem::InitRenderResource(std::shared_ptr<RTTextureResource> textureResource, RTTextureResourceDesc desc)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(textureResource != nullptr, "RenderSystem::InitRenderResource requires a texture resource.");

        EnqueueCommand("RenderSystemInitTextureResource",
                       [this, textureResource = std::move(textureResource), desc = std::move(desc)]() mutable
                       {
                           VE_ASSERT(impl_->rhi.device != nullptr);
                           RhiObjectList retiredObjects = textureResource->TakeRhiObjects();
                           textureResource->InitRenderResource(*impl_->rhi.device, std::move(desc));
                           RetireRhiObjects(*impl_, std::move(retiredObjects));
                       });
    }

    void RenderSystem::InitRenderResource(std::shared_ptr<RTMaterialResource> materialResource, RTMaterialResourceDesc desc)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(materialResource != nullptr, "RenderSystem::InitRenderResource requires a material resource.");

        EnqueueCommand("RenderSystemInitMaterialResource",
                       [this, materialResource = std::move(materialResource), desc = std::move(desc)]() mutable
                       { materialResource->InitRenderResource(std::move(desc)); });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTRenderTexture> renderTexture)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(renderTexture != nullptr, "RenderSystem::ReleaseRenderResource requires a render texture.");

        EnqueueCommand("RenderSystemReleaseRenderTexture",
                       [this, renderTexture = std::move(renderTexture)]() mutable { RetireRhiObjects(*impl_, renderTexture->TakeRhiObjects()); });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTScene> scene)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(scene != nullptr, "RenderSystem::ReleaseRenderResource requires a render scene.");
        EnqueueCommand("RenderSystemReleaseSceneResource", [this, scene = std::move(scene)]() mutable { RetireRhiObjects(*impl_, scene->TakeRhiObjects()); });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTRenderViewState> viewState)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(viewState != nullptr, "RenderSystem::ReleaseRenderResource requires a render view state.");
        EnqueueCommand("RenderSystemReleaseViewStateResource",
                       [this, viewState = std::move(viewState)]() mutable { RetireRhiObjects(*impl_, viewState->TakeRhiObjects()); });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTRenderItem> renderItem)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(renderItem != nullptr, "RenderSystem::ReleaseRenderResource requires a render item.");
        EnqueueCommand("RenderSystemReleaseRenderItemResource",
                       [this, renderItem = std::move(renderItem)]() mutable { RetireRhiObjects(*impl_, renderItem->TakeRhiObjects()); });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTMaterialResource> materialResource)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(materialResource != nullptr, "RenderSystem::ReleaseRenderResource requires a material resource.");

        EnqueueCommand("RenderSystemReleaseMaterialResource",
                       [this, materialResource = std::move(materialResource)]() mutable { RetireRhiObjects(*impl_, materialResource->TakeRhiObjects()); });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTMeshResource> meshResource)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(meshResource != nullptr, "RenderSystem::ReleaseRenderResource requires a mesh resource.");

        EnqueueCommand("RenderSystemReleaseMeshResource",
                       [this, meshResource = std::move(meshResource)]() mutable { RetireRhiObjects(*impl_, meshResource->TakeRhiObjects()); });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTShaderResource> shaderResource)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(shaderResource != nullptr, "RenderSystem::ReleaseRenderResource requires a shader resource.");

        EnqueueCommand("RenderSystemReleaseShaderResource",
                       [this, shaderResource = std::move(shaderResource)]() mutable { RetireRhiObjects(*impl_, shaderResource->TakeRhiObjects()); });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTTextureResource> textureResource)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(textureResource != nullptr, "RenderSystem::ReleaseRenderResource requires a texture resource.");

        EnqueueCommand("RenderSystemReleaseTextureResource",
                       [this, textureResource = std::move(textureResource)]() mutable { RetireRhiObjects(*impl_, textureResource->TakeRhiObjects()); });
    }

    void RenderSystem::RenderFrame(std::shared_ptr<FrameRenderPipeline> framePipeline)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(framePipeline != nullptr, "RenderSystem::RenderFrame requires a frame pipeline.");

        EnqueueCommand("RenderSystemRenderFrame", [this, framePipeline = std::move(framePipeline)]() { RenderMainSwapchainFrame(*impl_, framePipeline); });
    }

    void RenderSystem::EnqueueCommand(RenderCommand command)
    {
        EnqueueCommand(std::move(command.debugName), std::move(command.function));
    }

    void RenderSystem::Flush()
    {
        VE_ASSERT_MESSAGE(impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire),
                          "RenderSystem::Flush requires RenderSystem to accept commands.");

        auto completed = std::make_shared<ManualResetEvent>(false);
        EnqueueCommand("RenderSystemFlush", [completed]() { completed->Set(); });
        completed->Wait();
    }

    void RenderSystem::WaitIdle()
    {
        if (!impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        const ErrorCode result = ExecuteSynchronous("RenderSystemWaitIdle",
                                                    [this]()
                                                    {
                                                        if (impl_->rhi.device != nullptr)
                                                        {
                                                            impl_->rhi.device->WaitIdle();
                                                            ClearRetiredRhiObjectsAfterWaitIdle(*impl_);
                                                        }
                                                        return ErrorCode::None;
                                                    });
        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to wait for the GPU to become idle.");
    }

    ErrorCode RenderSystem::ExecuteSynchronous(std::string debugName, RenderSynchronousFunction function)
    {
        if (!function)
        {
            return ErrorCode::InvalidArgument;
        }

        if (!impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        auto completed = std::make_shared<ManualResetEvent>(false);
        auto operationResult = std::make_shared<ErrorCode>(ErrorCode::None);

        EnqueueCommand(std::move(debugName),
                       [completed, operationResult, function = std::move(function)]()
                       {
                           *operationResult = function();
                           completed->Set();
                       });

        completed->Wait();
        return *operationResult;
    }

    void RenderSystem::EnqueueCommand(std::string debugName, RenderCommandFunction function)
    {
        VE_ASSERT_MESSAGE(function != nullptr, "RenderSystem::EnqueueCommand requires a callable function.");

        impl_->threadControl.activeSubmitCount.fetch_add(1, std::memory_order_acq_rel);
        auto submitCounterGuard = MakeScopeExit([this]() { impl_->threadControl.activeSubmitCount.fetch_sub(1, std::memory_order_acq_rel); });

        VE_ASSERT_MESSAGE(impl_->threadControl.lifetimeFlags.acceptingCommands.load(std::memory_order_acquire),
                          "RenderSystem::EnqueueCommand requires RenderSystem to accept commands.");

        ErrorCode pushResult = impl_->threadControl.commandQueue.Push(RenderCommand{std::move(debugName), std::move(function)});
        VE_ASSERT_MESSAGE(pushResult == ErrorCode::None, "RenderSystem failed to enqueue render command.");

        impl_->threadControl.commandSemaphore.Release();
    }
} // namespace ve
