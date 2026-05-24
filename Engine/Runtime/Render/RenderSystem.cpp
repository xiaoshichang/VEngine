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
#include "Engine/Runtime/Render/RenderCommandQueue.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"

#include <exception>
#include <optional>
#include <string_view>
#include <vector>

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

        enum class RenderFrameSlotState
        {
            Free,
            Submitted,
            Rendering,
        };

        struct RenderFrameToken
        {
            UInt64 frameId = 0;
        };

        struct RenderFrameContext
        {
            UInt64 frameId = 0;
            RenderFrameSlotState state = RenderFrameSlotState::Free;
            ErrorCode lastError = ErrorCode::None;
            std::unique_ptr<rhi::RhiCommandList> commandList;
        };
    } // namespace

    struct RenderSystemImpl
    {
        struct ThreadState
        {
            Thread ownedThread;
            Atomic<UInt64> renderThreadIdValue{0};
            AtomicBool initialized{false};
            AtomicBool stopRequested{false};
        };

        struct CommandState
        {
            Semaphore semaphore{0};
            RenderCommandQueue queue;
            AtomicBool acceptingCommands{false};
            AtomicSize activeSubmitCount{0};
        };

        struct GameThreadBindingState
        {
            Atomic<UInt64> gameThreadIdValue{0};
        };

        struct FrameRingState
        {
            UInt32 maxFramesInFlight = DefaultMaxRenderFramesInFlight;
            UInt64 nextRenderFrameId = 0;
            std::vector<RenderFrameContext> contexts;
        };

        struct RhiState
        {
            struct TriangleResources
            {
                std::unique_ptr<rhi::RhiBuffer> vertexBuffer;
                std::unique_ptr<rhi::RhiShaderModule> vertexShader;
                std::unique_ptr<rhi::RhiShaderModule> fragmentShader;
                std::unique_ptr<rhi::RhiPipelineState> pipelineState;
            };

            Atomic<int> backendValue{-1};
            std::unique_ptr<rhi::RhiDevice> device;
            std::unique_ptr<rhi::RhiSwapchain> mainSwapchain;
            TriangleResources triangle;
        };

        ThreadState thread;
        CommandState commands;
        GameThreadBindingState gameThreadBinding;
        FrameRingState frameRing;
        RhiState rhi;
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

        [[noreturn]] void TerminateRenderSystem(std::string_view message, ErrorCode errorCode)
        {
            VE_LOG_FATAL("RenderSystem {}: {}", message, ToString(errorCode));
            std::terminate();
        }

        [[nodiscard]] ThreadId GetBoundGameThreadId(const RenderSystemImpl& impl) noexcept
        {
            return ThreadId{impl.gameThreadBinding.gameThreadIdValue.load(std::memory_order_acquire)};
        }

        [[nodiscard]] bool CheckGameThreadAccess(const RenderSystemImpl& impl) noexcept
        {
            const ThreadId gameThreadId = GetBoundGameThreadId(impl);
            return gameThreadId.IsValid() && GetCurrentThreadId() == gameThreadId;
        }

        void ValidateGameThreadAccess(const RenderSystemImpl& impl, const char* functionName) noexcept
        {
            VE_ASSERT_MESSAGE(CheckGameThreadAccess(impl), functionName);
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
            for (RenderFrameContext& frameContext : impl.frameRing.contexts)
            {
                frameContext.commandList.reset();
                frameContext.lastError = ErrorCode::None;
            }

            impl.rhi.triangle.pipelineState.reset();
            impl.rhi.triangle.fragmentShader.reset();
            impl.rhi.triangle.vertexShader.reset();
            impl.rhi.triangle.vertexBuffer.reset();
        }

        [[nodiscard]] ErrorCode CreateTriangleResources(RenderSystemImpl& impl)
        {
            VE_ASSERT_MESSAGE(impl.rhi.device != nullptr,
                              "CreateTriangleResources requires an initialized RHI device.");
            VE_ASSERT_MESSAGE(impl.rhi.mainSwapchain != nullptr,
                              "CreateTriangleResources requires an initialized main swapchain.");

            rhi::RhiBufferDesc vertexBufferDesc = {};
            vertexBufferDesc.size = sizeof(TriangleVertices);
            vertexBufferDesc.usage = rhi::RhiBufferUsage::Vertex;
            vertexBufferDesc.initialData = TriangleVertices;
            vertexBufferDesc.debugName = "RenderSystemTriangleVertexBuffer";

            impl.rhi.triangle.vertexBuffer = impl.rhi.device->CreateBuffer(vertexBufferDesc);
            if (impl.rhi.triangle.vertexBuffer == nullptr)
            {
                return ErrorCode::PlatformError;
            }

            rhi::RhiShaderModuleDesc vertexShaderDesc = {};
            vertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
            vertexShaderDesc.source = TriangleShaderSource;
            vertexShaderDesc.entryPoint = "VSMain";
            vertexShaderDesc.debugName = "RenderSystemTriangleVertexShader";

            impl.rhi.triangle.vertexShader = impl.rhi.device->CreateShaderModule(vertexShaderDesc);
            if (impl.rhi.triangle.vertexShader == nullptr)
            {
                DestroyTriangleResources(impl);
                return ErrorCode::PlatformError;
            }

            rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
            fragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
            fragmentShaderDesc.source = TriangleShaderSource;
            fragmentShaderDesc.entryPoint = "PSMain";
            fragmentShaderDesc.debugName = "RenderSystemTriangleFragmentShader";

            impl.rhi.triangle.fragmentShader = impl.rhi.device->CreateShaderModule(fragmentShaderDesc);
            if (impl.rhi.triangle.fragmentShader == nullptr)
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
            pipelineDesc.vertexShader = impl.rhi.triangle.vertexShader.get();
            pipelineDesc.fragmentShader = impl.rhi.triangle.fragmentShader.get();
            pipelineDesc.vertexLayout.attributes = &positionAttribute;
            pipelineDesc.vertexLayout.attributeCount = 1;
            pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
            pipelineDesc.topology = rhi::RhiPrimitiveTopology::TriangleList;
            pipelineDesc.colorFormat = impl.rhi.mainSwapchain->GetColorFormat();
            pipelineDesc.debugName = "RenderSystemTrianglePipeline";

            impl.rhi.triangle.pipelineState = impl.rhi.device->CreateGraphicsPipeline(pipelineDesc);
            if (impl.rhi.triangle.pipelineState == nullptr)
            {
                DestroyTriangleResources(impl);
                return ErrorCode::PlatformError;
            }

            for (RenderFrameContext& frameContext : impl.frameRing.contexts)
            {
                frameContext.commandList = impl.rhi.device->CreateCommandList();
                if (frameContext.commandList == nullptr)
                {
                    DestroyTriangleResources(impl);
                    return ErrorCode::PlatformError;
                }
            }

            return ErrorCode::None;
        }

        void RenderTriangleFrame(RenderSystemImpl& impl, RenderFrameContext& frameContext)
        {
            if (impl.rhi.device == nullptr || impl.rhi.mainSwapchain == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderTriangleFrame", ErrorCode::InvalidState);
            }

            if (impl.rhi.triangle.vertexBuffer == nullptr || impl.rhi.triangle.pipelineState == nullptr ||
                frameContext.commandList == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderTriangleFrame", ErrorCode::InvalidState);
            }

            rhi::RhiRenderPassDesc renderPassDesc = {};
            renderPassDesc.colorLoadAction = rhi::RhiLoadAction::Clear;
            renderPassDesc.colorStoreAction = rhi::RhiStoreAction::Store;
            renderPassDesc.clearColor = {0.05f, 0.07f, 0.10f, 1.0f};

            const rhi::RhiExtent2D extent = impl.rhi.mainSwapchain->GetExtent();
            rhi::RhiCommandList& commandList = *frameContext.commandList;

            if (!commandList.Begin() || !commandList.BeginRenderPass(*impl.rhi.mainSwapchain, renderPassDesc))
            {
                TerminateRenderSystem("frame execution failed in RenderTriangleFrame command list begin",
                                      ErrorCode::PlatformError);
            }

            commandList.SetViewport(rhi::RhiViewport{
                0.0f, 0.0f, static_cast<Float32>(extent.width), static_cast<Float32>(extent.height), 0.0f, 1.0f});
            commandList.SetScissor(rhi::RhiScissorRect{0, 0, extent.width, extent.height});
            commandList.SetPipeline(*impl.rhi.triangle.pipelineState);
            commandList.SetVertexBuffer(0, *impl.rhi.triangle.vertexBuffer, sizeof(TriangleVertex), 0);
            commandList.Draw(3, 0);
            commandList.EndRenderPass();

            if (!commandList.End() || !impl.rhi.device->Submit(commandList) || !impl.rhi.mainSwapchain->Present())
            {
                TerminateRenderSystem("frame execution failed in RenderTriangleFrame submit", ErrorCode::PlatformError);
            }
        }

        [[nodiscard]] UInt32 GetRenderFrameSlotIndex(UInt64 frameId, UInt32 maxFramesInFlight) noexcept
        {
            VE_ASSERT_MESSAGE(frameId > 0, "Render frame slot index requires a non-zero frame id.");
            VE_ASSERT_MESSAGE(maxFramesInFlight > 0, "Render frame slot index requires at least one frame slot.");
            return static_cast<UInt32>((frameId - 1) % static_cast<UInt64>(maxFramesInFlight));
        }

        [[nodiscard]] RenderFrameContext* FindRenderFrameContext(RenderSystemImpl& impl,
                                                                 const RenderFrameToken& token) noexcept
        {
            if (token.frameId == 0 || impl.frameRing.contexts.empty())
            {
                return nullptr;
            }

            const UInt32 slotIndex = GetRenderFrameSlotIndex(token.frameId, impl.frameRing.maxFramesInFlight);
            if (slotIndex >= impl.frameRing.contexts.size())
            {
                return nullptr;
            }

            RenderFrameContext& frameContext = impl.frameRing.contexts[slotIndex];
            if (frameContext.frameId != token.frameId)
            {
                return nullptr;
            }

            return &frameContext;
        }

        void AcquireRenderFrameSlot(RenderSystemImpl& impl, RenderFrameToken& outToken)
        {
            if (!impl.commands.acceptingCommands.load(std::memory_order_acquire))
            {
                TerminateRenderSystem("submission failed in RenderFrame acquire", ErrorCode::InvalidState);
            }

            ++impl.frameRing.nextRenderFrameId;
            const UInt32 slotIndex =
                GetRenderFrameSlotIndex(impl.frameRing.nextRenderFrameId, impl.frameRing.maxFramesInFlight);
            RenderFrameContext& frameContext = impl.frameRing.contexts[slotIndex];
            VE_ASSERT_MESSAGE(frameContext.state == RenderFrameSlotState::Free,
                              "Render frame ring slot should be free.");

            frameContext.frameId = impl.frameRing.nextRenderFrameId;
            frameContext.state = RenderFrameSlotState::Submitted;
            frameContext.lastError = ErrorCode::None;
            outToken.frameId = frameContext.frameId;
        }

        void MarkRenderFrameSlotRendering(RenderSystemImpl& impl, const RenderFrameToken& token) noexcept
        {
            RenderFrameContext* frameContext = FindRenderFrameContext(impl, token);
            VE_ASSERT_MESSAGE(frameContext != nullptr, "Render frame token must refer to a live frame context.");
            if (frameContext != nullptr)
            {
                frameContext->state = RenderFrameSlotState::Rendering;
            }
        }

        void ReleaseRenderFrameSlot(RenderSystemImpl& impl, const RenderFrameToken& token, ErrorCode result) noexcept
        {
            RenderFrameContext* frameContext = FindRenderFrameContext(impl, token);
            VE_ASSERT_MESSAGE(frameContext != nullptr, "Render frame slot release requires a live frame context.");
            if (frameContext != nullptr)
            {
                frameContext->lastError = result;
                frameContext->frameId = 0;
                frameContext->state = RenderFrameSlotState::Free;
            }
        }

        void ExecuteRenderFrame(RenderSystemImpl& impl, const RenderFrameToken& token) noexcept
        {
            MarkRenderFrameSlotRendering(impl, token);

            RenderFrameContext* frameContext = nullptr;
            frameContext = FindRenderFrameContext(impl, token);

            if (frameContext == nullptr)
            {
                TerminateRenderSystem("frame execution failed in RenderFrame", ErrorCode::InvalidState);
            }

            RenderTriangleFrame(impl, *frameContext);
        }

        void ExecuteCommand(RenderThreadContext& context, RenderCommand& command) noexcept
        {
            try
            {
                command.function(context);
            }
            catch (...)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped a RenderSystem command.");
            }
        }

        void RenderThreadLoop(RenderSystemImpl& impl)
        {
            const ThreadId renderThreadId = GetCurrentThreadId();
            impl.thread.renderThreadIdValue.store(renderThreadId.value, std::memory_order_release);
            RenderThreadContext context(renderThreadId);

            for (;;)
            {
                while (std::optional<RenderCommand> command = impl.commands.queue.TryPop())
                {
                    ExecuteCommand(context, *command);
                }

                if (impl.thread.stopRequested.load(std::memory_order_acquire))
                {
                    if (!impl.commands.queue.IsEmptyForConsumer())
                    {
                        continue;
                    }

                    break;
                }

                impl.commands.semaphore.Acquire();
            }

            while (std::optional<RenderCommand> command = impl.commands.queue.TryPop())
            {
                ExecuteCommand(context, *command);
            }
        }

        void DestroyRhiStateOnRenderThread(RenderSystemImpl& impl)
        {
            if (impl.rhi.device != nullptr)
            {
                impl.rhi.device->WaitIdle();
            }

            DestroyTriangleResources(impl);
            impl.rhi.mainSwapchain.reset();

            if (impl.rhi.device != nullptr)
            {
                impl.rhi.device.reset();
            }

            impl.rhi.backendValue.store(-1, std::memory_order_release);
        }

        void EnqueueInternalCommand(RenderSystemImpl& impl, RenderCommand command) noexcept
        {
            ErrorCode pushResult = impl.commands.queue.Push(std::move(command));
            VE_ASSERT_MESSAGE(pushResult == ErrorCode::None,
                              "RenderSystem failed to enqueue an internal render command.");
            impl.commands.semaphore.Release();
        }

        void StopAndJoinRenderThread(RenderSystemImpl& impl) noexcept
        {
            impl.commands.acceptingCommands.store(false, std::memory_order_release);

            while (impl.commands.activeSubmitCount.load(std::memory_order_acquire) != 0)
            {
                YieldThread();
            }

            auto rhiDestroyed = std::make_shared<ManualResetEvent>(false);
            EnqueueInternalCommand(impl,
                                   RenderCommand{"RenderSystemDestroyRhiState",
                                                 [&impl, rhiDestroyed](RenderThreadContext&)
                                                 {
                                                     DestroyRhiStateOnRenderThread(impl);
                                                     rhiDestroyed->Set();
                                                 }});
            rhiDestroyed->Wait();

            impl.thread.stopRequested.store(true, std::memory_order_release);
            impl.commands.semaphore.Release();

            if (impl.thread.ownedThread.IsJoinable())
            {
                const bool joined = impl.thread.ownedThread.Join();
                VE_ASSERT_MESSAGE(joined, "RenderSystem failed to join its Render Thread during shutdown.");
            }

            impl.commands.queue.ClearForConsumer();
            impl.thread.renderThreadIdValue.store(0, std::memory_order_release);
            impl.thread.stopRequested.store(false, std::memory_order_release);
            impl.thread.initialized.store(false, std::memory_order_release);
        }
    } // namespace

    RenderThreadContext::RenderThreadContext(ThreadId renderThreadId) noexcept
        : renderThreadId_(renderThreadId)
    {
    }

    ThreadId RenderThreadContext::GetRenderThreadId() const noexcept
    {
        return renderThreadId_;
    }

    RenderCommandFence::RenderCommandFence() = default;

    RenderCommandFence::~RenderCommandFence() = default;

    bool RenderCommandFence::IsComplete() const noexcept
    {
        return completed_ == nullptr || completed_->TryWait();
    }

    void RenderCommandFence::Wait() noexcept
    {
        if (completed_ != nullptr)
        {
            completed_->Wait();
        }
    }

    void RenderCommandFence::SetCompletionEvent(std::shared_ptr<ManualResetEvent> completed) noexcept
    {
        completed_ = std::move(completed);
    }

    RenderSystem::RenderSystem()
        : impl_(std::make_unique<RenderSystemImpl>())
    {
    }

    RenderSystem::~RenderSystem()
    {
        Shutdown();
    }

    void RenderSystem::Initialize(const RenderSystemDesc& desc)
    {
        if (impl_->thread.initialized.load(std::memory_order_acquire))
        {
            TerminateRenderSystem("initialization failed: RenderSystem is already initialized",
                                  ErrorCode::InvalidState);
        }

        if (desc.maxFramesInFlight == 0)
        {
            TerminateRenderSystem("initialization failed: maxFramesInFlight must be greater than zero",
                                  ErrorCode::InvalidArgument);
        }

        impl_->thread.stopRequested.store(false, std::memory_order_release);
        impl_->commands.acceptingCommands.store(true, std::memory_order_release);
        impl_->thread.renderThreadIdValue.store(0, std::memory_order_release);
        impl_->gameThreadBinding.gameThreadIdValue.store(0, std::memory_order_release);
        impl_->rhi.backendValue.store(-1, std::memory_order_release);
        impl_->frameRing.maxFramesInFlight = desc.maxFramesInFlight;

        impl_->frameRing.nextRenderFrameId = 0;
        impl_->frameRing.contexts.clear();
        try
        {
            impl_->frameRing.contexts.resize(desc.maxFramesInFlight);
        }
        catch (const std::bad_alloc&)
        {
            TerminateRenderSystem("initialization failed: failed to allocate render frame contexts",
                                  ErrorCode::OutOfMemory);
        }

        for (RenderFrameContext& frameContext : impl_->frameRing.contexts)
        {
            frameContext.state = RenderFrameSlotState::Free;
            frameContext.lastError = ErrorCode::None;
        }

        ErrorCode startResult = impl_->thread.ownedThread.Start(
            desc.threadName.empty() ? ThreadDesc{"VEngineRenderThread"} : ThreadDesc{desc.threadName},
            [this]() { RenderThreadLoop(*impl_); });

        if (startResult != ErrorCode::None)
        {
            TerminateRenderSystem("initialization failed: failed to start Render Thread", startResult);
        }

        impl_->thread.initialized.store(true, std::memory_order_release);
    }

    void RenderSystem::Shutdown() noexcept
    {
        if (!impl_->thread.initialized.load(std::memory_order_acquire))
        {
            return;
        }

        StopAndJoinRenderThread(*impl_);
    }

    void RenderSystem::BindGameThread(ThreadId gameThreadId) noexcept
    {
        VE_ASSERT_MESSAGE(gameThreadId.IsValid(), "RenderSystem::BindGameThread requires a valid Game Thread id.");
        impl_->gameThreadBinding.gameThreadIdValue.store(gameThreadId.value, std::memory_order_release);
    }

    void RenderSystem::ClearGameThreadBinding() noexcept
    {
        impl_->gameThreadBinding.gameThreadIdValue.store(0, std::memory_order_release);
    }

    void RenderSystem::BeginFrameEndFence(RenderCommandFence& fence)
    {
        ValidateGameThreadAccess(*impl_, "RenderSystem::BeginFrameEndFence must be called on the Game Thread.");

        auto completed = std::make_shared<ManualResetEvent>(false);
        fence.SetCompletionEvent(completed);
        SubmitFunction("RenderSystemFrameEndFence", [completed](RenderThreadContext&) { completed->Set(); });
    }

    bool RenderSystem::IsInitialized() const noexcept
    {
        return impl_->thread.initialized.load(std::memory_order_acquire);
    }

    ThreadId RenderSystem::GetRenderThreadId() const noexcept
    {
        return ThreadId{impl_->thread.renderThreadIdValue.load(std::memory_order_acquire)};
    }

    ErrorCode RenderSystem::InitializeDevice(const RenderDeviceDesc& desc)
    {
        return ExecuteSynchronous("RenderSystemInitializeDevice",
                                  [this, desc](RenderThreadContext&)
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
                                      impl_->rhi.backendValue.store(static_cast<int>(desc.backend),
                                                                    std::memory_order_release);
                                      VE_LOG_INFO("RenderSystem initialized RHI backend: {}", ToString(desc.backend));
                                      return ErrorCode::None;
                                  });
    }

    void RenderSystem::ShutdownDevice() noexcept
    {
        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        ErrorCode result = ExecuteSynchronous("RenderSystemShutdownDevice",
                                              [this](RenderThreadContext&)
                                              {
                                                  DestroyRhiStateOnRenderThread(*impl_);
                                                  return ErrorCode::None;
                                              });

        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to shut down its RHI device.");
    }

    bool RenderSystem::HasDevice() const noexcept
    {
        return impl_->rhi.backendValue.load(std::memory_order_acquire) >= 0;
    }

    RenderBackend RenderSystem::GetDeviceBackend() const noexcept
    {
        const int backendValue = impl_->rhi.backendValue.load(std::memory_order_acquire);
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
                                  [this, desc](RenderThreadContext&)
                                  {
                                      if (impl_->rhi.device == nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      if (impl_->rhi.mainSwapchain != nullptr)
                                      {
                                          return ErrorCode::InvalidState;
                                      }

                                      std::unique_ptr<rhi::RhiSwapchain> swapchain =
                                          impl_->rhi.device->CreateSwapchain(ToRhiSwapchainDesc(desc));
                                      if (swapchain == nullptr)
                                      {
                                          return ErrorCode::PlatformError;
                                      }

                                      impl_->rhi.mainSwapchain = std::move(swapchain);
                                      ErrorCode triangleResult = CreateTriangleResources(*impl_);
                                      if (triangleResult != ErrorCode::None)
                                      {
                                          impl_->rhi.mainSwapchain.reset();
                                          return triangleResult;
                                      }

                                      return ErrorCode::None;
                                  });
    }

    void RenderSystem::DestroyMainSwapchain() noexcept
    {
        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            return;
        }

        ErrorCode result = ExecuteSynchronous("RenderSystemDestroyMainSwapchain",
                                              [this](RenderThreadContext&)
                                              {
                                                  if (impl_->rhi.device != nullptr)
                                                  {
                                                      impl_->rhi.device->WaitIdle();
                                                  }

                                                  DestroyTriangleResources(*impl_);
                                                  impl_->rhi.mainSwapchain.reset();
                                                  return ErrorCode::None;
                                              });

        VE_ASSERT_MESSAGE(result == ErrorCode::None, "RenderSystem failed to destroy its main swapchain.");
    }

    void RenderSystem::RenderFrame()
    {
        ValidateGameThreadAccess(*impl_, "RenderSystem::RenderFrame must be called on the Game Thread.");

        RenderFrameToken token;
        AcquireRenderFrameSlot(*impl_, token);

        SubmitFunction("RenderSystemRenderFrame",
                       [this, token](RenderThreadContext&)
                       {
                           ExecuteRenderFrame(*impl_, token);
                           ReleaseRenderFrameSlot(*impl_, token, ErrorCode::None);
                       });
    }

    UInt32 RenderSystem::GetMaxRenderFramesInFlight() const noexcept
    {
        return impl_->frameRing.maxFramesInFlight;
    }

    SizeT RenderSystem::GetRenderFramesInFlight() const noexcept
    {
        return 0;
    }

    void RenderSystem::Submit(RenderCommand command)
    {
        ValidateGameThreadAccess(*impl_, "RenderSystem::Submit must be called on the Game Thread.");
        SubmitFunction(std::move(command.debugName), std::move(command.function));
    }

    ErrorCode RenderSystem::Flush()
    {
        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        auto completed = std::make_shared<ManualResetEvent>(false);
        SubmitFunction("RenderSystemFlush", [completed](RenderThreadContext&) { completed->Set(); });

        completed->Wait();
        return ErrorCode::None;
    }

    ErrorCode RenderSystem::ExecuteSynchronous(std::string debugName, RenderSynchronousFunction function)
    {
        if (!function)
        {
            return ErrorCode::InvalidArgument;
        }

        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        auto completed = std::make_shared<ManualResetEvent>(false);
        auto operationResult = std::make_shared<ErrorCode>(ErrorCode::None);

        SubmitFunction(std::move(debugName),
                       [completed, operationResult, function = std::move(function)](RenderThreadContext& context)
                       {
                           *operationResult = function(context);
                           completed->Set();
                       });

        completed->Wait();
        return *operationResult;
    }

    void RenderSystem::SubmitFunction(std::string debugName, RenderCommandFunction function)
    {
        if (!function)
        {
            TerminateRenderSystem("submission failed: invalid command function", ErrorCode::InvalidArgument);
        }

        impl_->commands.activeSubmitCount.fetch_add(1, std::memory_order_acq_rel);
        auto submitCounterGuard =
            MakeScopeExit([this]() { impl_->commands.activeSubmitCount.fetch_sub(1, std::memory_order_acq_rel); });

        if (!impl_->commands.acceptingCommands.load(std::memory_order_acquire))
        {
            TerminateRenderSystem("submission failed: RenderSystem is not accepting commands", ErrorCode::InvalidState);
        }

        ErrorCode pushResult = impl_->commands.queue.Push(RenderCommand{std::move(debugName), std::move(function)});
        if (pushResult != ErrorCode::None)
        {
            TerminateRenderSystem("submission failed in RenderCommandQueue::Push", pushResult);
        }

        impl_->commands.semaphore.Release();
    }
} // namespace ve
