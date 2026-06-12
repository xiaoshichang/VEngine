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
#include "Engine/Runtime/Render/FrameRenderer.h"
#include "Engine/Runtime/Render/RenderCommandQueue.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"
#include "Engine/Runtime/Threading/Synchronization.h"

#include <exception>
#include <optional>

namespace ve
{
    namespace
    {
        struct TriangleVertex
        {
            Float32 position[3] = {};
        };

        constexpr TriangleVertex TriangleVertices[] = {
            TriangleVertex{{0.0f, 0.5f, 0.0f}},
            TriangleVertex{{0.5f, -0.5f, 0.0f}},
            TriangleVertex{{-0.5f, -0.5f, 0.0f}},
        };

        const char* TriangleShaderSource = R"(
struct VSInput
{
    float3 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = float3(input.position.x + 0.5f, input.position.y + 0.5f, 1.0f);
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(input.color, 1.0f);
}
)";
    } // namespace

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
        std::unique_ptr<rhi::RhiBuffer> triangleVertexBuffer;
        std::unique_ptr<rhi::RhiShaderModule> triangleVertexShader;
        std::unique_ptr<rhi::RhiShaderModule> triangleFragmentShader;
        std::unique_ptr<rhi::RhiPipelineState> trianglePipelineState;
        std::unique_ptr<rhi::RhiCommandList> frameCommandList;
        FrameRenderer frameRenderer;
    };

    namespace
    {
        class TriangleRenderPass final : public RenderPass
        {
        public:
            TriangleRenderPass(rhi::RhiBuffer& vertexBuffer, rhi::RhiPipelineState& pipelineState)
                : vertexBuffer_(&vertexBuffer)
                , pipelineState_(&pipelineState)
            {
            }

            [[nodiscard]] const char* GetName() const noexcept override
            {
                return "TriangleForwardPass";
            }

            void Setup(RenderPassBuilder& builder) override
            {
                builder.AddSwapchainColorAttachment(rhi::RhiLoadAction::Clear,
                                                    rhi::RhiStoreAction::Store,
                                                    rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f});
            }

            void Execute(RenderPassContext& context) override
            {
                VE_ASSERT(vertexBuffer_ != nullptr);
                VE_ASSERT(pipelineState_ != nullptr);

                rhi::RhiCommandList& commandList = context.GetCommandList();
                commandList.SetPipeline(*pipelineState_);
                commandList.SetVertexBuffer(0, *vertexBuffer_, sizeof(TriangleVertex), 0);
                commandList.Draw(3, 0);
            }

        private:
            rhi::RhiBuffer* vertexBuffer_ = nullptr;
            rhi::RhiPipelineState* pipelineState_ = nullptr;
        };

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

        void DestroyTriangleResources(RenderSystemImpl& impl)
        {
            impl.frameRenderer.ClearPasses();
            impl.frameCommandList.reset();
            impl.trianglePipelineState.reset();
            impl.triangleFragmentShader.reset();
            impl.triangleVertexShader.reset();
            impl.triangleVertexBuffer.reset();
        }

        [[nodiscard]] ErrorCode CreateTriangleResources(RenderSystemImpl& impl)
        {
            VE_ASSERT_MESSAGE(impl.device != nullptr, "CreateTriangleResources requires an initialized RHI device.");
            VE_ASSERT_MESSAGE(impl.mainSwapchain != nullptr,
                              "CreateTriangleResources requires an initialized main swapchain.");

            rhi::RhiBufferDesc vertexBufferDesc = {};
            vertexBufferDesc.size = sizeof(TriangleVertices);
            vertexBufferDesc.usage = rhi::RhiBufferUsage::Vertex;
            vertexBufferDesc.initialData = TriangleVertices;
            vertexBufferDesc.debugName = "RenderSystemTriangleVertexBuffer";

            impl.triangleVertexBuffer = impl.device->CreateBuffer(vertexBufferDesc);
            if (impl.triangleVertexBuffer == nullptr)
            {
                return ErrorCode::PlatformError;
            }

            rhi::RhiShaderModuleDesc vertexShaderDesc = {};
            vertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
            vertexShaderDesc.source = TriangleShaderSource;
            vertexShaderDesc.entryPoint = "VSMain";
            vertexShaderDesc.debugName = "RenderSystemTriangleVertexShader";

            impl.triangleVertexShader = impl.device->CreateShaderModule(vertexShaderDesc);
            if (impl.triangleVertexShader == nullptr)
            {
                DestroyTriangleResources(impl);
                return ErrorCode::PlatformError;
            }

            rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
            fragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
            fragmentShaderDesc.source = TriangleShaderSource;
            fragmentShaderDesc.entryPoint = "PSMain";
            fragmentShaderDesc.debugName = "RenderSystemTriangleFragmentShader";

            impl.triangleFragmentShader = impl.device->CreateShaderModule(fragmentShaderDesc);
            if (impl.triangleFragmentShader == nullptr)
            {
                DestroyTriangleResources(impl);
                return ErrorCode::PlatformError;
            }

            rhi::RhiVertexAttributeDesc positionAttribute = {};
            positionAttribute.semanticName = "POSITION";
            positionAttribute.semanticIndex = 0;
            positionAttribute.format = rhi::RhiFormat::Rgb32Float;
            positionAttribute.offset = 0;

            rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
            pipelineDesc.vertexShader = impl.triangleVertexShader.get();
            pipelineDesc.fragmentShader = impl.triangleFragmentShader.get();
            pipelineDesc.vertexLayout.attributes = &positionAttribute;
            pipelineDesc.vertexLayout.attributeCount = 1;
            pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
            pipelineDesc.topology = rhi::RhiPrimitiveTopology::TriangleList;
            pipelineDesc.colorFormat = impl.mainSwapchain->GetColorFormat();
            pipelineDesc.debugName = "RenderSystemTrianglePipeline";

            impl.trianglePipelineState = impl.device->CreateGraphicsPipeline(pipelineDesc);
            if (impl.trianglePipelineState == nullptr)
            {
                DestroyTriangleResources(impl);
                return ErrorCode::PlatformError;
            }

            impl.frameCommandList = impl.device->CreateCommandList();
            if (impl.frameCommandList == nullptr)
            {
                DestroyTriangleResources(impl);
                return ErrorCode::PlatformError;
            }

            impl.frameRenderer.ClearPasses();
            impl.frameRenderer.AddPass(
                std::make_unique<TriangleRenderPass>(*impl.triangleVertexBuffer, *impl.trianglePipelineState));

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode BeginMainSwapchainFrame(RenderSystemImpl& impl)
        {
            VE_ASSERT_RENDER_THREAD();
            VE_ASSERT(impl.device != nullptr);
            VE_ASSERT(impl.mainSwapchain != nullptr);
            VE_ASSERT(impl.frameCommandList != nullptr);

            return impl.frameRenderer.BeginFrame(*impl.frameCommandList, *impl.mainSwapchain);
        }

        [[nodiscard]] ErrorCode DrawTriangleFrame(RenderSystemImpl& impl)
        {
            VE_ASSERT_RENDER_THREAD();
            VE_ASSERT(impl.triangleVertexBuffer != nullptr);
            VE_ASSERT(impl.trianglePipelineState != nullptr);
            VE_ASSERT(impl.frameCommandList != nullptr);

            return impl.frameRenderer.ExecutePassesInOrder();
        }

        [[nodiscard]] ErrorCode EndMainSwapchainFrame(RenderSystemImpl& impl)
        {
            VE_ASSERT_RENDER_THREAD();
            VE_ASSERT(impl.device != nullptr);
            VE_ASSERT(impl.mainSwapchain != nullptr);
            VE_ASSERT(impl.frameCommandList != nullptr);

            impl.frameRenderer.EndFrame();

            auto ok = impl.device->Submit(*impl.frameCommandList);
            VE_ASSERT(ok);

            ok = impl.mainSwapchain->Present();
            VE_ASSERT(ok);

            return ErrorCode::None;
        }

        void ExecuteCommand(RenderCommand& command) noexcept
        {
            VE_ASSERT_RENDER_THREAD();
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

            DestroyTriangleResources(impl);
            impl.mainSwapchain.reset();

            if (impl.device != nullptr)
            {
                impl.device.reset();
            }

            impl.backendValue.store(-1, std::memory_order_release);
        }

        void EnqueueInternalCommand(RenderSystemImpl& impl, RenderCommand command) noexcept
        {
            ErrorCode pushResult = impl.commandQueue.Push(std::move(command));
            VE_ASSERT_MESSAGE(pushResult == ErrorCode::None,
                              "RenderSystem failed to enqueue an internal render command.");
            impl.commandSemaphore.Release();
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

        ErrorCode startResult = impl_->thread.Start(initParam.threadName.empty() ? ThreadDesc{"VEngineRenderThread"}
                                                                                 : ThreadDesc{initParam.threadName},
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
        VE_ASSERT_MESSAGE(!impl_->initialized.load(std::memory_order_acquire),
                          "SetSceneThreadRenderThreadFrameEndSync requires RenderSystem to be stopped.");
        impl_->sceneThreadRenderThreadFrameEndSync = sync;
    }

    ErrorCode RenderSystem::SubmitFrameEndFenceSignal(UInt32 fenceIndex)
    {
        VE_ASSERT_SCENE_THREAD();
        return EnqueueCommand("RenderSystemFrameEndFenceSignal",
                              [sync = impl_->sceneThreadRenderThreadFrameEndSync, fenceIndex]()
                              {
                                  sync->NotifyRenderThreadFrameEnd(fenceIndex);
                              });
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
                                      impl_->backendValue.store(static_cast<int>(desc.backend),
                                                                std::memory_order_release);
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

                                      outHandles.backend = static_cast<RenderBackend>(
                                          impl_->backendValue.load(std::memory_order_acquire));
                                      outHandles.hasMainSwapchain = impl_->mainSwapchain != nullptr;
                                      outHandles.device = impl_->device->GetNativeDeviceHandle();
                                      outHandles.immediateContext = impl_->device->GetNativeImmediateContextHandle();
                                      outHandles.graphicsQueue = impl_->device->GetNativeGraphicsQueueHandle();
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

                                      std::unique_ptr<rhi::RhiSwapchain> swapchain =
                                          impl_->device->CreateSwapchain(ToRhiSwapchainDesc(desc));
                                      if (swapchain == nullptr)
                                      {
                                          return ErrorCode::PlatformError;
                                      }

                                      impl_->mainSwapchain = std::move(swapchain);
                                      ErrorCode triangleResult = CreateTriangleResources(*impl_);
                                      if (triangleResult != ErrorCode::None)
                                      {
                                          impl_->mainSwapchain.reset();
                                          return triangleResult;
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

                                                  DestroyTriangleResources(*impl_);
                                                  impl_->mainSwapchain.reset();
                                                  return ErrorCode::None;
                                              });

        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to destroy its main swapchain.");
    }

    ErrorCode RenderSystem::BeginRenderFrame()
    {
        VE_ASSERT_SCENE_THREAD();

        ErrorCode submitResult = EnqueueCommand("RenderSystemBeginRenderFrame",
                                                [this]()
                                                {
                                                    const ErrorCode result = BeginMainSwapchainFrame(*impl_);
                                                    VE_ASSERT_MESSAGE(result == ErrorCode::None,
                                                                      "RenderSystem::BeginRenderFrame failed.");
                                                });
        VE_ASSERT_MESSAGE(submitResult == ErrorCode::None, "RenderSystem::BeginRenderFrame enqueue failed.");
        return submitResult;
    }

    ErrorCode RenderSystem::EndRenderFrame()
    {
        VE_ASSERT_SCENE_THREAD();

        ErrorCode submitResult = EnqueueCommand("RenderSystemEndRenderFrame",
                                                [this]()
                                                {
                                                    const ErrorCode result = EndMainSwapchainFrame(*impl_);
                                                    VE_ASSERT_MESSAGE(result == ErrorCode::None,
                                                                      "RenderSystem::EndRenderFrame failed.");
                                                });
        VE_ASSERT_MESSAGE(submitResult == ErrorCode::None, "RenderSystem::EndRenderFrame enqueue failed.");
        return submitResult;
    }

    ErrorCode RenderSystem::RenderFrame()
    {
        VE_ASSERT_SCENE_THREAD();
        ErrorCode submitResult = EnqueueCommand("RenderSystemRenderTriangleFrame",
                                                [this]()
                                                {
                                                    const ErrorCode result = DrawTriangleFrame(*impl_);
                                                    VE_ASSERT_MESSAGE(result == ErrorCode::None,
                                                                      "RenderSystem::RenderFrame failed.");
                                                });
        VE_ASSERT_MESSAGE(submitResult == ErrorCode::None, "RenderSystem::RenderFrame enqueue failed.");
        return submitResult;
    }

    ErrorCode RenderSystem::EnqueueCommand(RenderCommand command)
    {
        return EnqueueCommand(std::move(command.debugName), std::move(command.function));
    }

    ErrorCode RenderSystem::Flush()
    {
        if (!impl_->acceptingCommands.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        auto completed = std::make_shared<ManualResetEvent>(false);
        ErrorCode submitResult = EnqueueCommand("RenderSystemFlush", [completed]() { completed->Set(); });

        if (submitResult != ErrorCode::None)
        {
            return submitResult;
        }

        completed->Wait();
        return ErrorCode::None;
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

        ErrorCode submitResult = EnqueueCommand(std::move(debugName),
                                                [completed, operationResult, function = std::move(function)]()
                                                {
                                                    *operationResult = function();
                                                    completed->Set();
                                                });

        if (submitResult != ErrorCode::None)
        {
            return submitResult;
        }

        completed->Wait();
        return *operationResult;
    }

    ErrorCode RenderSystem::EnqueueCommand(std::string debugName, RenderCommandFunction function)
    {
        if (!function)
        {
            return ErrorCode::InvalidArgument;
        }

        impl_->activeSubmitCount.fetch_add(1, std::memory_order_acq_rel);
        auto submitCounterGuard =
            MakeScopeExit([this]() { impl_->activeSubmitCount.fetch_sub(1, std::memory_order_acq_rel); });

        if (!impl_->acceptingCommands.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        ErrorCode pushResult = impl_->commandQueue.Push(RenderCommand{std::move(debugName), std::move(function)});
        if (pushResult != ErrorCode::None)
        {
            return pushResult;
        }

        impl_->commandSemaphore.Release();
        return ErrorCode::None;
    }
} // namespace ve
