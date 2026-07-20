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
#include "Engine/Runtime/Render/MaterialUniformPool.h"
#include "Engine/Runtime/Render/RenderCommandQueue.h"
#include "Engine/Runtime/Render/RenderFramePipeline.h"
#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <array>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace ve
{
    struct RenderSystemImpl
    {
        Thread thread;
        Atomic<UInt64> renderThreadIdValue{0};
        // frame sync between scene thread
        SceneThreadRenderThreadFrameEndSync* sceneThreadRenderThreadFrameEndSync = nullptr;

        Semaphore commandSemaphore{0};
        RenderCommandQueue commandQueue;
        AtomicBool acceptingCommands{false};
        AtomicBool stopRequested{false};
        AtomicBool initialized{false};
        AtomicSize activeSubmitCount{0};
        Atomic<int> backendValue{-1};
        std::unique_ptr<rhi::RhiDevice> device;
        std::unique_ptr<rhi::RhiSwapchain> mainSwapchain;
        std::array<FrameContext, RenderFrameContextCount> frameContexts;
        MaterialUniformPool materialUniformPool;
        ShaderManager shaderManager;
        std::vector<std::unique_ptr<rhi::RhiObject>> pendingRetiredResources;
        Atomic<UInt64> pendingMainSwapchainExtent{0};
        AtomicBool mainSwapchainResizeCommandQueued{false};
        UInt64 nextFrameIndex = 1;
    };

    namespace
    {
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

        void DestroyFrameResources(RenderSystemImpl& impl)
        {
            for (FrameContext& frameContext : impl.frameContexts)
            {
                const bool shutdown = frameContext.Shutdown();
                VE_ASSERT_MESSAGE(shutdown, "Failed to shut down a render frame context.");
            }
        }

        [[nodiscard]] bool WaitForAllFrameContexts(RenderSystemImpl& impl)
        {
            for (FrameContext& frameContext : impl.frameContexts)
            {
                if (frameContext.IsInitialized() && !frameContext.WaitAndReset())
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] ErrorCode CreateFrameResources(RenderSystemImpl& impl)
        {
            VE_ASSERT_MESSAGE(impl.device != nullptr, "CreateFrameResources requires an initialized RHI device.");

            for (UInt32 contextIndex = 0; contextIndex < RenderFrameContextCount; ++contextIndex)
            {
                if (!impl.frameContexts[contextIndex].Initialize(*impl.device, contextIndex))
                {
                    DestroyFrameResources(impl);
                    return ErrorCode::PlatformError;
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode RenderMainSwapchainFrame(RenderSystemImpl& impl, FrameRenderPipeline& framePipeline)
        {
            VE_ASSERT_RENDER_THREAD();
            VE_ASSERT(impl.device != nullptr);
            VE_ASSERT(impl.mainSwapchain != nullptr);
            const UInt64 frameIndex = impl.nextFrameIndex++;
            FrameContext& frameContext = impl.frameContexts[frameIndex % RenderFrameContextCount];
            if (!frameContext.WaitAndReset())
            {
                return ErrorCode::PlatformError;
            }

            FrameRenderPipelineData frameData = {};
            frameData.frameIndex = frameIndex;
            frameData.device = impl.device.get();
            frameData.mainSwapchain = impl.mainSwapchain.get();
            frameData.shaderManager = &impl.shaderManager;
            frameData.frameContext = &frameContext;

            ErrorCode renderResult = framePipeline.RenderFrame(frameData);
            if (renderResult != ErrorCode::None)
            {
                return renderResult;
            }

            const UInt64 submissionFenceValue = frameContext.GetNextSubmissionFenceValue();
            auto ok = impl.device->Submit(frameContext.GetCommandList(), &frameContext.GetCompletionFence(), submissionFenceValue);
            if (!ok)
            {
                impl.device->WaitIdle();
                impl.pendingRetiredResources.clear();
                return ErrorCode::PlatformError;
            }

            for (std::unique_ptr<rhi::RhiObject>& resource : impl.pendingRetiredResources)
            {
                frameContext.RetainTransientResource(std::move(resource));
            }
            impl.pendingRetiredResources.clear();
            frameContext.MarkSubmitted(submissionFenceValue);

            ok = impl.mainSwapchain->Present();
            if (!ok)
            {
                return ErrorCode::PlatformError;
            }

            return ErrorCode::None;
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
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped a RenderSystem command.");
            }
        }

        void RenderThreadLoop(RenderSystemImpl& impl)
        {
            const ThreadId renderThreadId = GetCurrentThreadId();
            impl.renderThreadIdValue.store(renderThreadId.value, std::memory_order_release);
            SetExpectedRenderThreadId(renderThreadId);

            for (;;)
            {
                while (std::optional<RenderCommand> command = impl.commandQueue.TryPop())
                {
                    ExecuteCommand(*command);
                }

                if (impl.stopRequested.load(std::memory_order_acquire))
                {
                    if (!impl.commandQueue.IsEmptyForConsumer())
                    {
                        continue;
                    }

                    break;
                }

                impl.commandSemaphore.Acquire();
            }

            while (std::optional<RenderCommand> command = impl.commandQueue.TryPop())
            {
                ExecuteCommand(*command);
            }

            SetExpectedRenderThreadId(ThreadId{});
        }

        void DestroyRhiStateOnRenderThread(RenderSystemImpl& impl)
        {
            VE_ASSERT_RENDER_THREAD();

            if (impl.device != nullptr)
            {
                impl.device->WaitIdle();
            }

            impl.pendingRetiredResources.clear();
            impl.pendingMainSwapchainExtent.store(0, std::memory_order_release);
            impl.mainSwapchainResizeCommandQueued.store(false, std::memory_order_release);
            impl.shaderManager.Clear();
            DestroyFrameResources(impl);
            impl.mainSwapchain.reset();
            impl.materialUniformPool.Shutdown();

            if (impl.device != nullptr)
            {
                impl.device.reset();
            }

            impl.backendValue.store(-1, std::memory_order_release);
        }

        void EnqueueInternalCommand(RenderSystemImpl& impl, RenderCommand command) noexcept
        {
            ErrorCode pushResult = impl.commandQueue.Push(std::move(command));
            VE_ASSERT_MESSAGE(pushResult == ErrorCode::None, "RenderSystem failed to enqueue an internal render command.");
            impl.commandSemaphore.Release();
        }

        void QueueMainSwapchainResizeCommand(RenderSystemImpl& impl);

        void ProcessMainSwapchainResize(RenderSystemImpl& impl)
        {
            VE_ASSERT_RENDER_THREAD();

            UInt64 packedExtent = impl.pendingMainSwapchainExtent.exchange(0, std::memory_order_acq_rel);
            if (packedExtent != 0 && impl.mainSwapchain != nullptr)
            {
                rhi::RhiExtent2D requestedExtent = UnpackExtent(packedExtent);
                const rhi::RhiExtent2D currentExtent = impl.mainSwapchain->GetExtent();
                if (requestedExtent.width != currentExtent.width || requestedExtent.height != currentExtent.height)
                {
                    if (impl.device != nullptr)
                    {
                        // Frame completion fences are queued before Present. Waiting for the device's internal fence
                        // here also covers presentation work that may still reference the old back buffers.
                        impl.device->WaitIdle();
                    }

                    if (!WaitForAllFrameContexts(impl))
                    {
                        VE_LOG_ERROR_CATEGORY("Render", "Failed to wait for in-flight frames before resizing the main swapchain.");
                    }
                    else
                    {
                        const UInt64 newerPackedExtent = impl.pendingMainSwapchainExtent.exchange(0, std::memory_order_acq_rel);
                        if (newerPackedExtent != 0)
                        {
                            requestedExtent = UnpackExtent(newerPackedExtent);
                        }

                        const rhi::RhiExtent2D resizedFromExtent = impl.mainSwapchain->GetExtent();
                        if ((requestedExtent.width != resizedFromExtent.width || requestedExtent.height != resizedFromExtent.height) &&
                            !impl.mainSwapchain->Resize(requestedExtent))
                        {
                            const char* backendError = impl.device != nullptr ? impl.device->GetLastErrorMessage() : nullptr;
                            VE_LOG_ERROR_CATEGORY("Render",
                                                  "Failed to resize the main swapchain to {}x{}. Backend error: {}",
                                                  requestedExtent.width,
                                                  requestedExtent.height,
                                                  backendError != nullptr && backendError[0] != '\0' ? backendError : "Unknown");
                        }
                    }
                }
            }

            impl.mainSwapchainResizeCommandQueued.store(false, std::memory_order_release);
            if (impl.pendingMainSwapchainExtent.load(std::memory_order_acquire) == 0 ||
                !impl.acceptingCommands.load(std::memory_order_acquire))
            {
                return;
            }

            bool expected = false;
            if (impl.mainSwapchainResizeCommandQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            {
                QueueMainSwapchainResizeCommand(impl);
            }
        }

        void QueueMainSwapchainResizeCommand(RenderSystemImpl& impl)
        {
            EnqueueInternalCommand(
                impl, RenderCommand{"RenderSystemResizeMainSwapchain", [&impl]() { ProcessMainSwapchainResize(impl); }});
        }

        void StopAndJoinRenderThread(RenderSystemImpl& impl) noexcept
        {
            impl.acceptingCommands.store(false, std::memory_order_release);

            while (impl.activeSubmitCount.load(std::memory_order_acquire) != 0)
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

            impl.stopRequested.store(true, std::memory_order_release);
            impl.commandSemaphore.Release();

            if (impl.thread.IsJoinable())
            {
                const bool joined = impl.thread.Join();
                VE_ASSERT_MESSAGE(joined, "RenderSystem failed to join its Render Thread during shutdown.");
            }

            impl.commandQueue.ClearForConsumer();
            impl.renderThreadIdValue.store(0, std::memory_order_release);
            impl.stopRequested.store(false, std::memory_order_release);
            impl.initialized.store(false, std::memory_order_release);
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
        if (impl_->initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        impl_->stopRequested.store(false, std::memory_order_release);
        impl_->acceptingCommands.store(true, std::memory_order_release);
        if (impl_->sceneThreadRenderThreadFrameEndSync != nullptr)
        {
            impl_->sceneThreadRenderThreadFrameEndSync->Reset();
        }

        ErrorCode startResult = impl_->thread.Start(initParam.threadName.empty() ? ThreadDesc{"VEngineRenderThread"} : ThreadDesc{initParam.threadName},
                                                    [this]() { RenderThreadLoop(*impl_); });

        if (startResult != ErrorCode::None)
        {
            impl_->acceptingCommands.store(false, std::memory_order_release);
            impl_->stopRequested.store(false, std::memory_order_release);
            impl_->commandQueue.ClearForConsumer();
            return startResult;
        }

        impl_->initialized.store(true, std::memory_order_release);
        return ErrorCode::None;
    }

    void RenderSystem::Shutdown() noexcept
    {
        if (!impl_->initialized.load(std::memory_order_acquire))
        {
            return;
        }

        StopAndJoinRenderThread(*impl_);
    }

    bool RenderSystem::IsInitialized() const noexcept
    {
        return impl_->initialized.load(std::memory_order_acquire);
    }

    ThreadId RenderSystem::GetRenderThreadId() const noexcept
    {
        return ThreadId{impl_->renderThreadIdValue.load(std::memory_order_acquire)};
    }

    void RenderSystem::SetSceneThreadRenderThreadFrameEndSync(SceneThreadRenderThreadFrameEndSync* sync) noexcept
    {
        VE_ASSERT_MESSAGE(!impl_->initialized.load(std::memory_order_acquire), "SetSceneThreadRenderThreadFrameEndSync requires RenderSystem to be stopped.");
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
                                      if (impl_->device != nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      std::unique_ptr<rhi::RhiDevice> device = CreateRhiDevice(desc);
                                      if (device == nullptr)
                                      {
                                          return ErrorCode::Unsupported;
                                      }

                                      impl_->device = std::move(device);
                                      impl_->materialUniformPool.Initialize(*impl_->device);
                                      impl_->backendValue.store(static_cast<int>(desc.backend), std::memory_order_release);
                                      VE_LOG_INFO("RenderSystem initialized RHI backend: {}", ToString(desc.backend));
                                      return ErrorCode::None;
                                  });
    }

    ErrorCode RenderSystem::QueryNativeHandles(RenderNativeHandles& outHandles)
    {
        return ExecuteSynchronous("RenderSystemQueryNativeHandles",
                                  [this, &outHandles]()
                                  {
                                      if (impl_->device == nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      outHandles.backend = static_cast<RenderBackend>(impl_->backendValue.load(std::memory_order_acquire));
                                      outHandles.hasMainSwapchain = impl_->mainSwapchain != nullptr;
                                      outHandles.device = impl_->device->GetNativeDeviceHandle();
                                      outHandles.immediateContext = impl_->device->GetNativeImmediateContextHandle();
                                      outHandles.graphicsQueue = impl_->device->GetNativeGraphicsQueueHandle();
                                      outHandles.shaderResourceDescriptorAllocator = impl_->device->GetNativeShaderResourceDescriptorAllocator();
                                      if (impl_->mainSwapchain != nullptr)
                                      {
                                          outHandles.mainSwapchainBufferCount = impl_->mainSwapchain->GetBufferCount();
                                          outHandles.mainSwapchainColorFormat = impl_->mainSwapchain->GetColorFormat();
                                      }
                                      return ErrorCode::None;
                                  });
    }

    void RenderSystem::ShutdownDevice() noexcept
    {
        if (!impl_->acceptingCommands.load(std::memory_order_acquire))
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
        const int backendValue = impl_->backendValue.load(std::memory_order_acquire);
        VE_ASSERT_MESSAGE(backendValue >= 0, "RenderSystem::GetDeviceBackend requires an initialized RHI device.");
        return static_cast<RenderBackend>(backendValue);
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
                                      if (impl_->device == nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      if (impl_->mainSwapchain != nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      std::unique_ptr<rhi::RhiSwapchain> swapchain = impl_->device->CreateSwapchain(ToRhiSwapchainDesc(desc));
                                      if (swapchain == nullptr)
                                      {
                                          return ErrorCode::PlatformError;
                                      }

                                      impl_->mainSwapchain = std::move(swapchain);
                                      ErrorCode frameResourceResult = CreateFrameResources(*impl_);
                                      if (frameResourceResult != ErrorCode::None)
                                      {
                                          impl_->mainSwapchain.reset();
                                          return frameResourceResult;
                                      }

                                      return ErrorCode::None;
                                  });
    }

    void RenderSystem::DestroyMainSwapchain() noexcept
    {
        if (!impl_->acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        ErrorCode result = ExecuteSynchronous("RenderSystemDestroyMainSwapchain",
                                              [this]()
                                              {
                                                  if (impl_->device != nullptr)
                                                  {
                                                      impl_->device->WaitIdle();
                                                  }

                                                  DestroyFrameResources(*impl_);
                                                  impl_->mainSwapchain.reset();
                                                  return ErrorCode::None;
                                              });

        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to destroy its main swapchain.");
    }

    void RenderSystem::RequestMainSwapchainResize(rhi::RhiExtent2D extent)
    {
        VE_ASSERT_SCENE_THREAD();
        if (extent.width == 0 || extent.height == 0 || !impl_->acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        impl_->pendingMainSwapchainExtent.store(PackExtent(extent), std::memory_order_release);
        bool expected = false;
        if (impl_->mainSwapchainResizeCommandQueued.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
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
                           VE_ASSERT(impl_->device != nullptr);
                           renderTexture->InitRenderResource(*impl_->device, std::move(desc), impl_->pendingRetiredResources);
                       });
    }

    void RenderSystem::InitRenderResource(std::shared_ptr<RTMeshResource> meshResource, RTMeshResourceDesc desc)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(meshResource != nullptr, "RenderSystem::InitRenderResource requires a mesh resource.");

        EnqueueCommand("RenderSystemInitMeshResource",
                       [this, meshResource = std::move(meshResource), desc = std::move(desc)]() mutable
                       {
                           VE_ASSERT(impl_->device != nullptr);
                           meshResource->InitRenderResource(*impl_->device, std::move(desc));
                       });
    }

    void RenderSystem::InitRenderResource(std::shared_ptr<RTShaderResource> shaderResource, RTShaderResourceDesc desc)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(shaderResource != nullptr, "RenderSystem::InitRenderResource requires a shader resource.");

        EnqueueCommand("RenderSystemInitShaderResource",
                       [this, shaderResource = std::move(shaderResource), desc = std::move(desc)]() mutable
                       {
                           VE_ASSERT(impl_->device != nullptr);
                           shaderResource->InitRenderResource(*impl_->device, std::move(desc));
                       });
    }

    void RenderSystem::InitRenderResource(std::shared_ptr<RTMaterialResource> materialResource, RTMaterialResourceDesc desc)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(materialResource != nullptr, "RenderSystem::InitRenderResource requires a material resource.");

        EnqueueCommand("RenderSystemInitMaterialResource",
                       [this, materialResource = std::move(materialResource), desc = std::move(desc)]() mutable
                       {
                           if (materialResource->IsInitialized())
                           {
                               const bool waitResult = WaitForAllFrameContexts(*impl_);
                               VE_ASSERT_MESSAGE(waitResult, "Failed to wait for in-flight frames before updating a material uniform.");
                               if (!waitResult)
                               {
                                   return;
                               }
                           }
                           materialResource->InitRenderResource(impl_->materialUniformPool, std::move(desc));
                       });
    }

    void RenderSystem::ReleaseRenderResource(std::shared_ptr<RTMaterialResource> materialResource)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(materialResource != nullptr, "RenderSystem::ReleaseRenderResource requires a material resource.");

        EnqueueCommand("RenderSystemReleaseMaterialResource",
                       [this, materialResource = std::move(materialResource)]()
                       {
                           if (!materialResource->IsInitialized())
                           {
                               return;
                           }

                           const bool waitResult = WaitForAllFrameContexts(*impl_);
                           VE_ASSERT_MESSAGE(waitResult, "Failed to wait for in-flight frames before releasing a material uniform.");
                           if (waitResult)
                           {
                               materialResource->ResetRenderResource(impl_->materialUniformPool);
                           }
                       });
    }

    void RenderSystem::RenderFrame(std::shared_ptr<FrameRenderPipeline> framePipeline)
    {
        VE_ASSERT_SCENE_THREAD();
        VE_ASSERT_MESSAGE(framePipeline != nullptr, "RenderSystem::RenderFrame requires a frame pipeline.");

        EnqueueCommand("RenderSystemRenderFrame",
                       [this, framePipeline = std::move(framePipeline)]()
                       {
                           const ErrorCode result = RenderMainSwapchainFrame(*impl_, *framePipeline);
                           if (result != ErrorCode::None)
                           {
                               std::string message = "RenderSystem::RenderFrame failed: ";
                               message += ToString(result);
                               if (impl_->device != nullptr)
                               {
                                   const char* backendError = impl_->device->GetLastErrorMessage();
                                   if (backendError != nullptr && backendError[0] != '\0')
                                   {
                                       message += ". Backend error: ";
                                       message += backendError;
                                   }
                               }
                               VE_LOG_ERROR_CATEGORY("Render", message);
                               VE_ASSERT_MESSAGE(false, message.c_str());
                           }
                       });
    }

    void RenderSystem::EnqueueCommand(RenderCommand command)
    {
        EnqueueCommand(std::move(command.debugName), std::move(command.function));
    }

    void RenderSystem::Flush()
    {
        VE_ASSERT_MESSAGE(impl_->acceptingCommands.load(std::memory_order_acquire), "RenderSystem::Flush requires RenderSystem to accept commands.");

        auto completed = std::make_shared<ManualResetEvent>(false);
        EnqueueCommand("RenderSystemFlush", [completed]() { completed->Set(); });
        completed->Wait();
    }

    void RenderSystem::WaitIdle()
    {
        if (!impl_->acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        const ErrorCode result = ExecuteSynchronous("RenderSystemWaitIdle",
                                                    [this]()
                                                    {
                                                        if (impl_->device != nullptr)
                                                        {
                                                            impl_->device->WaitIdle();
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

        if (!impl_->acceptingCommands.load(std::memory_order_acquire))
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

        impl_->activeSubmitCount.fetch_add(1, std::memory_order_acq_rel);
        auto submitCounterGuard = MakeScopeExit([this]() { impl_->activeSubmitCount.fetch_sub(1, std::memory_order_acq_rel); });

        VE_ASSERT_MESSAGE(impl_->acceptingCommands.load(std::memory_order_acquire), "RenderSystem::EnqueueCommand requires RenderSystem to accept commands.");

        ErrorCode pushResult = impl_->commandQueue.Push(RenderCommand{std::move(debugName), std::move(function)});
        VE_ASSERT_MESSAGE(pushResult == ErrorCode::None, "RenderSystem failed to enqueue render command.");

        impl_->commandSemaphore.Release();
    }
} // namespace ve
