#include "Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class PreviewTestSwapchain final : public ve::rhi::RhiSwapchain
    {
    public:
        [[nodiscard]] ve::rhi::RhiExtent2D GetExtent() const noexcept override
        {
            return {128, 64};
        }
        [[nodiscard]] ve::rhi::RhiFormat GetColorFormat() const noexcept override
        {
            return ve::rhi::RhiFormat::Bgra8Unorm;
        }
        [[nodiscard]] uint32_t GetBufferCount() const noexcept override
        {
            return 2;
        }
        [[nodiscard]] bool Resize(ve::rhi::RhiExtent2D) override
        {
            return true;
        }
        [[nodiscard]] bool Present() override
        {
            return true;
        }
    };

    class PreviewTestTexture final : public ve::rhi::RhiTexture
    {
    public:
        explicit PreviewTestTexture(ve::rhi::RhiTextureDesc desc)
            : desc_(desc)
        {
        }

        [[nodiscard]] ve::rhi::RhiTextureDimension GetDimension() const noexcept override
        {
            return desc_.dimension;
        }
        [[nodiscard]] uint32_t GetWidth() const noexcept override
        {
            return desc_.width;
        }
        [[nodiscard]] uint32_t GetHeight() const noexcept override
        {
            return desc_.height;
        }
        [[nodiscard]] ve::rhi::RhiFormat GetFormat() const noexcept override
        {
            return desc_.format;
        }
        [[nodiscard]] void* GetNativeSampledViewHandle() const noexcept override
        {
            return reinterpret_cast<void*>(0x1234);
        }

    private:
        ve::rhi::RhiTextureDesc desc_;
    };

    class PreviewTestShader final : public ve::rhi::RhiShaderModule
    {
    public:
        explicit PreviewTestShader(ve::rhi::RhiShaderStage stage)
            : stage_(stage)
        {
        }

        [[nodiscard]] ve::rhi::RhiShaderStage GetStage() const noexcept override
        {
            return stage_;
        }

    private:
        ve::rhi::RhiShaderStage stage_;
    };

    class PreviewTestPipeline final : public ve::rhi::RhiPipelineState
    {
    public:
        [[nodiscard]] ve::rhi::RhiPrimitiveTopology GetTopology() const noexcept override
        {
            return ve::rhi::RhiPrimitiveTopology::TriangleList;
        }
    };

    class PreviewTestDevice final : public ve::rhi::RhiDevice
    {
    public:
        [[nodiscard]] ve::rhi::RhiBackend GetBackend() const noexcept override
        {
            return ve::rhi::RhiBackend::D3D11;
        }
        [[nodiscard]] const char* GetLastErrorMessage() const noexcept override
        {
            return "preview test allocation failed";
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiSwapchain> CreateSwapchain(const ve::rhi::RhiSwapchainDesc&) override
        {
            return nullptr;
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiBuffer> CreateBuffer(const ve::rhi::RhiBufferDesc&) override
        {
            return nullptr;
        }
        void UpdateBuffer(ve::rhi::RhiBuffer&, uint64_t, const void*, uint64_t, ve::rhi::RhiBufferUpdateMode) override {}
        [[nodiscard]] bool ReadBuffer(const ve::rhi::RhiBuffer&, uint64_t, void*, uint64_t) override
        {
            return false;
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiTexture> CreateTexture(const ve::rhi::RhiTextureDesc& desc) override
        {
            ++createTextureCallCount;
            lastTextureDesc = desc;
            lastDebugName = desc.debugName != nullptr ? desc.debugName : "";
            return failTextureCreation ? nullptr : std::make_unique<PreviewTestTexture>(desc);
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiSampler> CreateSampler(const ve::rhi::RhiSamplerDesc&) override
        {
            return nullptr;
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiShaderModule> CreateShaderModule(const ve::rhi::RhiShaderModuleDesc& desc) override
        {
            ++createShaderCallCount;
            return failShaderCreation ? nullptr : std::make_unique<PreviewTestShader>(desc.stage);
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiPipelineState> CreateGraphicsPipeline(const ve::rhi::RhiGraphicsPipelineDesc&) override
        {
            ++createPipelineCallCount;
            return failPipelineCreation ? nullptr : std::make_unique<PreviewTestPipeline>();
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiComputePipelineState> CreateComputePipeline(const ve::rhi::RhiComputePipelineDesc&) override
        {
            return nullptr;
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiCommandList> CreateCommandList() override
        {
            return nullptr;
        }
        [[nodiscard]] std::unique_ptr<ve::rhi::RhiFence> CreateFence(uint64_t) override
        {
            return nullptr;
        }
        [[nodiscard]] bool Submit(ve::rhi::RhiCommandList&, ve::rhi::RhiFence*, uint64_t) override
        {
            return false;
        }
        void WaitIdle() override {}

        bool failTextureCreation = false;
        bool failShaderCreation = false;
        bool failPipelineCreation = false;
        ve::UInt32 createTextureCallCount = 0;
        ve::UInt32 createShaderCallCount = 0;
        ve::UInt32 createPipelineCallCount = 0;
        ve::rhi::RhiTextureDesc lastTextureDesc = {};
        std::string lastDebugName;
    };

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestPreviewScaleValidation()
    {
        bool passed = true;
        passed &= Expect(ve::IsFrameGraphDebugPreviewScaleValid(0.1F), "0.1 scale should be valid");
        passed &= Expect(ve::IsFrameGraphDebugPreviewScaleValid(1.0F), "1.0 scale should be valid");
        passed &= Expect(!ve::IsFrameGraphDebugPreviewScaleValid(0.09F), "scale below 0.1 should be rejected");
        passed &= Expect(!ve::IsFrameGraphDebugPreviewScaleValid(1.01F), "scale above 1.0 should be rejected");
        passed &= Expect(!ve::IsFrameGraphDebugPreviewScaleValid(std::numeric_limits<ve::Float32>::quiet_NaN()), "NaN scale should be rejected");
        passed &= Expect(!ve::IsFrameGraphDebugPreviewScaleValid(std::numeric_limits<ve::Float32>::infinity()), "infinite scale should be rejected");
        return passed;
    }

    bool TestPreviewExtent()
    {
        bool passed = true;
        const ve::rhi::RhiExtent2D oddExtent = ve::CalculateFrameGraphDebugPreviewExtent(3, 5, 0.5F);
        passed &= Expect(oddExtent.width == 2 && oddExtent.height == 3, "preview dimensions should round to nearest");

        const ve::rhi::RhiExtent2D minimumExtent = ve::CalculateFrameGraphDebugPreviewExtent(1, 1, 0.1F);
        passed &= Expect(minimumExtent.width == 1 && minimumExtent.height == 1, "preview dimensions should clamp to one pixel");
        return passed;
    }

    bool TestPreviewModeSelectionCoversEveryRhiFormat()
    {
        struct FormatExpectation
        {
            ve::rhi::RhiFormat format;
            ve::FrameGraphDebugPreviewMode mode;
        };

        const FormatExpectation expectations[] = {
            {ve::rhi::RhiFormat::Unknown, ve::FrameGraphDebugPreviewMode::Unsupported},
            {ve::rhi::RhiFormat::Rgba8Unorm, ve::FrameGraphDebugPreviewMode::Color},
            {ve::rhi::RhiFormat::Bgra8Unorm, ve::FrameGraphDebugPreviewMode::Color},
            {ve::rhi::RhiFormat::Rgb32Float, ve::FrameGraphDebugPreviewMode::Color},
            {ve::rhi::RhiFormat::R32Uint, ve::FrameGraphDebugPreviewMode::UnsignedInteger},
            {ve::rhi::RhiFormat::Depth32Float, ve::FrameGraphDebugPreviewMode::Depth},
        };

        bool passed = true;
        for (const FormatExpectation& expectation : expectations)
        {
            passed &= Expect(ve::SelectFrameGraphDebugPreviewMode(expectation.format) == expectation.mode,
                             "preview mode selection should classify every current RHI format");
        }
        return passed;
    }

    bool TestDepthPreviewContrastRemap()
    {
        const ve::Float32 clearDepth = ve::RemapFrameGraphDebugDepthForPreview(1.0F);
        const ve::Float32 nearDepth = ve::RemapFrameGraphDebugDepthForPreview(0.0F);
        const ve::Float32 typicalPerspectiveDepth = ve::RemapFrameGraphDebugDepthForPreview(0.99F);

        bool passed = true;
        passed &= Expect(clearDepth == 0.0F, "depth preview should render the cleared far plane as black");
        passed &= Expect(nearDepth == 1.0F, "depth preview should render the near plane as white");
        passed &= Expect(typicalPerspectiveDepth > 0.25F && typicalPerspectiveDepth < 0.75F,
                         "depth preview should lift common perspective depths into a visible gray range");
        return passed;
    }

    bool TestPreviewStagingSelectionUsesSampledFlagPresence()
    {
        bool passed = true;
        passed &= Expect(!ve::NeedsFrameGraphDebugStaging(ve::rhi::RhiTextureUsage::Sampled), "sampled textures should not require staging");
        const auto sampledRenderTarget = static_cast<ve::rhi::RhiTextureUsage>(static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::Sampled) |
                                                                               static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::RenderTarget));
        passed &= Expect(!ve::NeedsFrameGraphDebugStaging(sampledRenderTarget), "combined sampled render targets should not require staging");
        passed &= Expect(ve::NeedsFrameGraphDebugStaging(ve::rhi::RhiTextureUsage::RenderTarget), "render-target-only textures should require staging");
        passed &= Expect(ve::NeedsFrameGraphDebugStaging(ve::rhi::RhiTextureUsage::DepthStencil), "depth-only textures should require staging");
        passed &= Expect(ve::NeedsFrameGraphDebugStaging(ve::rhi::RhiTextureUsage::Storage), "storage-only textures should require staging");
        return passed;
    }

    bool TestPreviewTextureOwnerInitializationAndReset()
    {
        PreviewTestDevice device;
        ve::FrameGraphDebugPreviewTexture preview;
        std::vector<std::unique_ptr<ve::rhi::RhiObject>> retiredResources;

        bool passed = true;
        passed &= Expect(preview.Initialize(device, {0, 8}, "ZeroPreview") == ve::ErrorCode::InvalidArgument, "preview owner should reject a zero width");
        passed &= Expect(device.createTextureCallCount == 0, "invalid preview extent should not allocate a texture");

        passed &= Expect(preview.Initialize(device, {64, 32}, "DebugPreview") == ve::ErrorCode::None, "preview owner should initialize a valid texture");
        const ve::UInt32 expectedUsage =
            static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::Sampled) | static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::RenderTarget);
        passed &= Expect(device.createTextureCallCount == 1 && device.lastTextureDesc.dimension == ve::rhi::RhiTextureDimension::Texture2D &&
                             device.lastTextureDesc.width == 64 && device.lastTextureDesc.height == 32 && device.lastTextureDesc.depth == 1 &&
                             device.lastTextureDesc.mipLevelCount == 1 && device.lastTextureDesc.format == ve::rhi::RhiFormat::Rgba8Unorm &&
                             static_cast<ve::UInt32>(device.lastTextureDesc.usage) == expectedUsage && device.lastDebugName == "DebugPreview",
                         "preview owner should allocate the exact persistent preview texture descriptor");
        passed &= Expect(preview.GetTexture() != nullptr && preview.GetTexture()->GetWidth() == 64, "preview owner should expose its initialized texture");
        passed &=
            Expect(preview.GetNativeSampledViewHandle() == reinterpret_cast<void*>(0x1234), "preview owner should publish its native sampled view handle");

        preview.Reset(retiredResources);
        passed &= Expect(preview.GetTexture() == nullptr && preview.GetNativeSampledViewHandle() == nullptr && retiredResources.size() == 1,
                         "preview reset should clear publication and retire its texture");
        preview.Reset(retiredResources);
        passed &= Expect(retiredResources.size() == 1, "repeated preview reset should not retire anything twice");

        PreviewTestDevice failingDevice;
        failingDevice.failTextureCreation = true;
        ve::FrameGraphDebugPreviewTexture failedPreview;
        passed &= Expect(failedPreview.Initialize(failingDevice, {16, 16}, "FailedPreview") == ve::ErrorCode::OutOfMemory,
                         "preview allocation failure should return an error");
        passed &= Expect(failedPreview.GetTexture() == nullptr && failedPreview.GetNativeSampledViewHandle() == nullptr,
                         "failed preview initialization should keep the owner empty");
        return passed;
    }

    bool TestShaderManagerNonFatalCachePaths()
    {
        PreviewTestDevice device;
        ve::ShaderManager shaderManager;
        const ve::rhi::RhiShaderModuleDesc shaderDesc = {
            ve::rhi::RhiShaderStage::Vertex, ve::rhi::RhiShaderCodeFormat::Source, "void VSMain() {}", nullptr, 0, "VSMain", "PreviewTestVS"};

        bool passed = true;
        ve::rhi::RhiShaderModule* shader = shaderManager.TryGetOrCompileShader(device, ve::ShaderID{"FrameGraphDebugPreview.TestShader", 0}, shaderDesc);
        passed &= Expect(shader != nullptr && device.createShaderCallCount == 1, "non-fatal shader cache path should create a missing shader");
        passed &= Expect(shaderManager.TryGetOrCompileShader(device, ve::ShaderID{"FrameGraphDebugPreview.TestShader", 0}, shaderDesc) == shader &&
                             device.createShaderCallCount == 1,
                         "non-fatal shader cache path should reuse a cached shader");

        device.failShaderCreation = true;
        passed &= Expect(shaderManager.TryGetOrCompileShader(device, ve::ShaderID{"FrameGraphDebugPreview.FailedShader", 0}, shaderDesc) == nullptr,
                         "non-fatal shader cache path should return null on compilation failure");

        ve::rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.boundShaderState.vertexShader = shader;
        ve::rhi::RhiPipelineState* pipeline =
            shaderManager.TryGetOrCreateGraphicsPipeline(device, ve::GraphicsPipelineID{"FrameGraphDebugPreview.TestPipeline", 0}, pipelineDesc);
        passed &= Expect(pipeline != nullptr && device.createPipelineCallCount == 1, "non-fatal pipeline cache path should create a missing pipeline");
        passed &= Expect(shaderManager.TryGetOrCreateGraphicsPipeline(device, ve::GraphicsPipelineID{"FrameGraphDebugPreview.TestPipeline", 0}, pipelineDesc) ==
                                 pipeline &&
                             device.createPipelineCallCount == 1,
                         "non-fatal pipeline cache path should reuse a cached pipeline");

        device.failPipelineCreation = true;
        passed &= Expect(
            shaderManager.TryGetOrCreateGraphicsPipeline(device, ve::GraphicsPipelineID{"FrameGraphDebugPreview.FailedPipeline", 0}, pipelineDesc) == nullptr,
            "non-fatal pipeline cache path should return null on creation failure");
        shaderManager.Clear();
        return passed;
    }

    bool TestCaptureRequestLifecycle()
    {
        ve::FrameGraphDebugCaptureExchange exchange;

        bool passed = true;
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Idle, "exchange should start idle");
        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "valid capture request should succeed");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Armed, "valid capture request should arm the exchange");

        const std::optional<ve::FrameGraphDebugCaptureRequest> request = exchange.ConsumeRequest();
        passed &= Expect(request.has_value(), "armed capture request should be consumable");
        passed &= Expect(request && request->requestId != 0, "capture request should receive a nonzero ID");
        passed &= Expect(request && request->previewScale == 0.5F, "capture request should preserve the preview scale");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Capturing, "consuming a request should start capture");
        passed &= Expect(!exchange.ConsumeRequest().has_value(), "capture request should only be consumed once");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Capturing, "second consume should preserve the active capture");
        return passed;
    }

    bool TestCaptureRequestRejection()
    {
        ve::FrameGraphDebugCaptureExchange exchange;

        bool passed = true;
        passed &= Expect(exchange.RequestCapture(0.09F) == ve::ErrorCode::InvalidArgument, "scale below the minimum should be rejected");
        passed &= Expect(exchange.RequestCapture(1.01F) == ve::ErrorCode::InvalidArgument, "scale above the maximum should be rejected");
        passed &= Expect(exchange.RequestCapture(std::numeric_limits<ve::Float32>::quiet_NaN()) == ve::ErrorCode::InvalidArgument,
                         "NaN capture scale should be rejected");
        passed &= Expect(exchange.RequestCapture(std::numeric_limits<ve::Float32>::infinity()) == ve::ErrorCode::InvalidArgument,
                         "infinite capture scale should be rejected");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Idle, "invalid requests should preserve idle state");

        passed &= Expect(exchange.RequestCapture(0.25F) == ve::ErrorCode::None, "first valid capture request should succeed");
        passed &= Expect(exchange.RequestCapture(0.75F) == ve::ErrorCode::InvalidState, "second armed capture request should be rejected");
        const std::optional<ve::FrameGraphDebugCaptureRequest> request = exchange.ConsumeRequest();
        passed &= Expect(request && request->previewScale == 0.25F, "rejected armed request should not replace the original");
        passed &= Expect(exchange.RequestCapture(1.0F) == ve::ErrorCode::InvalidState, "request during capture should be rejected");
        passed &= Expect(!exchange.ConsumeRequest().has_value(), "rejected capturing request should not arm another request");

        auto data = std::make_shared<ve::FrameGraphDebugData>();
        data->requestId = request ? request->requestId : 0;
        (void)exchange.Publish(data);
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Ready, "rejected request should preserve the active request ID");
        return passed;
    }

    bool TestMatchingPublishAndTake()
    {
        ve::FrameGraphDebugCaptureExchange exchange;
        bool passed = true;
        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "capture request should succeed before publish");
        const std::optional<ve::FrameGraphDebugCaptureRequest> request = exchange.ConsumeRequest();

        auto data = std::make_shared<ve::FrameGraphDebugData>();
        data->requestId = request ? request->requestId : 0;
        (void)exchange.Publish(data);

        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Ready, "matching publish should complete the capture");
        const std::shared_ptr<const ve::FrameGraphDebugData> taken = exchange.TakePublishedData();
        passed &= Expect(taken == data, "take should return the exact published data");
        passed &= Expect(!exchange.TakePublishedData(), "published data should only be taken once");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Ready, "take should preserve ready completion status");
        return passed;
    }

    bool TestInvalidPublishPreservesActiveCaptureAndPriorData()
    {
        ve::FrameGraphDebugCaptureExchange exchange;
        bool passed = true;
        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "initial request should succeed");
        const std::optional<ve::FrameGraphDebugCaptureRequest> firstRequest = exchange.ConsumeRequest();
        auto firstData = std::make_shared<ve::FrameGraphDebugData>();
        firstData->requestId = firstRequest ? firstRequest->requestId : 0;
        (void)exchange.Publish(firstData);

        passed &= Expect(exchange.RequestCapture(0.75F) == ve::ErrorCode::None, "new request after ready should succeed");
        const std::optional<ve::FrameGraphDebugCaptureRequest> secondRequest = exchange.ConsumeRequest();
        (void)exchange.Publish(nullptr);
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Capturing, "null publish should preserve active capture");

        auto staleData = std::make_shared<ve::FrameGraphDebugData>();
        staleData->requestId = firstRequest ? firstRequest->requestId : 0;
        (void)exchange.Publish(staleData);
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Capturing, "stale publish should not complete a newer capture");
        passed &= Expect(exchange.TakePublishedData() == firstData, "invalid publish should not replace previously published data");

        auto secondData = std::make_shared<ve::FrameGraphDebugData>();
        secondData->requestId = secondRequest ? secondRequest->requestId : 0;
        (void)exchange.Publish(secondData);
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Ready, "matching newer publish should complete capture");
        passed &= Expect(exchange.TakePublishedData() == secondData, "matching newer publish should replace the published result");

        (void)exchange.Publish(firstData);
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Ready, "publish without an active capture should preserve status");
        passed &= Expect(!exchange.TakePublishedData(), "stale publish after completion should not restore old data");
        return passed;
    }

    bool TestCaptureFailureAndRecovery()
    {
        ve::FrameGraphDebugCaptureExchange exchange;
        bool passed = true;
        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "request before failure should succeed");
        const std::optional<ve::FrameGraphDebugCaptureRequest> firstRequest = exchange.ConsumeRequest();
        exchange.Fail(firstRequest ? firstRequest->requestId : 0, "capture failed");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Failed, "matching failure should fail capture");
        passed &= Expect(exchange.GetFailureMessage() == "capture failed", "matching failure should retain its message");

        passed &= Expect(exchange.RequestCapture(0.75F) == ve::ErrorCode::None, "new request after failure should succeed");
        passed &= Expect(exchange.GetFailureMessage().empty(), "new request should clear prior failure text");
        const std::optional<ve::FrameGraphDebugCaptureRequest> secondRequest = exchange.ConsumeRequest();
        passed &=
            Expect(firstRequest && secondRequest && secondRequest->requestId > firstRequest->requestId, "request IDs should increase after failure recovery");

        exchange.Fail(firstRequest ? firstRequest->requestId : 0, "stale failure");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Capturing, "stale failure should not fail a newer capture");
        passed &= Expect(exchange.GetFailureMessage().empty(), "stale failure should not replace the newer capture state");

        exchange.Fail(secondRequest ? secondRequest->requestId : 0, "new failure");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Failed, "matching newer failure should fail capture");
        passed &= Expect(exchange.GetFailureMessage() == "new failure", "matching newer failure should retain its message");

        (void)exchange.Reset();
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Idle, "reset after failure should restore idle state");
        passed &= Expect(exchange.GetFailureMessage().empty(), "reset after failure should clear failure text");
        return passed;
    }

    bool TestCaptureReset()
    {
        ve::FrameGraphDebugCaptureExchange exchange;
        bool passed = true;
        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "request before reset should succeed");
        const std::optional<ve::FrameGraphDebugCaptureRequest> request = exchange.ConsumeRequest();
        auto data = std::make_shared<ve::FrameGraphDebugData>();
        data->requestId = request ? request->requestId : 0;
        (void)exchange.Publish(data);

        (void)exchange.Reset();
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Idle, "reset should restore idle state");
        passed &= Expect(exchange.GetFailureMessage().empty(), "reset should clear failure text");
        passed &= Expect(!exchange.ConsumeRequest().has_value(), "reset should clear pending requests");
        passed &= Expect(!exchange.TakePublishedData(), "reset should clear published data");

        passed &= Expect(exchange.RequestCapture(1.0F) == ve::ErrorCode::None, "armed request before another reset should succeed");
        (void)exchange.Reset();
        passed &= Expect(!exchange.ConsumeRequest().has_value(), "reset should clear an armed request");

        passed &= Expect(exchange.RequestCapture(1.0F) == ve::ErrorCode::None, "request after resetting an armed exchange should succeed");
        const std::optional<ve::FrameGraphDebugCaptureRequest> requestAfterReset = exchange.ConsumeRequest();
        passed &= Expect(request && requestAfterReset && requestAfterReset->requestId > request->requestId,
                         "reset should not reuse request IDs during the exchange lifetime");

        (void)exchange.Reset();
        exchange.Fail(requestAfterReset ? requestAfterReset->requestId : 0, "failure after reset");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Idle, "stale failure after reset should preserve idle state");
        return passed;
    }

    bool TestFrameGraphDebugSubmitGate()
    {
        const auto data = std::make_shared<ve::FrameGraphDebugData>();

        bool passed = true;
        passed &= Expect(!ve::ShouldPublishFrameGraphDebugCapture(ve::ErrorCode::PlatformError, data, {}), "failed submit must not publish");
        passed &= Expect(!ve::ShouldPublishFrameGraphDebugCapture(ve::ErrorCode::None, nullptr, {}), "missing data must not publish");
        passed &= Expect(!ve::ShouldPublishFrameGraphDebugCapture(ve::ErrorCode::None, data, "conversion failed"), "capture failure must not publish");
        passed &= Expect(ve::ShouldPublishFrameGraphDebugCapture(ve::ErrorCode::None, data, {}), "successful submitted capture should publish");
        return passed;
    }

    bool TestFrameGraphDebugPreviewCollectionDeduplicatesOwners()
    {
        ve::FrameGraphDebugData data;
        data.textures.resize(2);
        data.textures[0].versions.resize(2);
        data.textures[1].versions.resize(1);

        const auto sharedPreview = std::make_shared<ve::FrameGraphDebugPreviewTexture>();
        const auto uniquePreview = std::make_shared<ve::FrameGraphDebugPreviewTexture>();
        data.textures[0].versions[0].preview.texture = sharedPreview;
        data.textures[0].versions[1].preview.texture = sharedPreview;
        data.textures[1].versions[0].preview.texture = uniquePreview;

        const std::vector<std::shared_ptr<ve::FrameGraphDebugPreviewTexture>> previews = ve::CollectFrameGraphDebugPreviewTextures(data);
        bool passed = true;
        passed &= Expect(previews.size() == 2, "preview collection should contain each owner exactly once");
        passed &= Expect(std::count(previews.begin(), previews.end(), sharedPreview) == 1, "duplicated version owners should be deduplicated");
        passed &= Expect(std::count(previews.begin(), previews.end(), uniquePreview) == 1, "unique preview owner should be retained");
        return passed;
    }

    bool TestCapturePublishAndResetTransferDisplacedOwnership()
    {
        ve::FrameGraphDebugCaptureExchange exchange;
        bool passed = true;

        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "first ownership-transfer request should succeed");
        const auto firstRequest = exchange.ConsumeRequest();
        auto firstData = std::make_shared<ve::FrameGraphDebugData>();
        firstData->requestId = firstRequest ? firstRequest->requestId : 0;
        const ve::FrameGraphDebugCapturePublishResult firstPublish = exchange.Publish(firstData);
        passed &= Expect(firstPublish.published && !firstPublish.dataToRetire, "first publish should succeed without displacing data");

        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "replacement request should succeed");
        const auto secondRequest = exchange.ConsumeRequest();
        auto secondData = std::make_shared<ve::FrameGraphDebugData>();
        secondData->requestId = secondRequest ? secondRequest->requestId : 0;
        const ve::FrameGraphDebugCapturePublishResult replacementPublish = exchange.Publish(secondData);
        passed &= Expect(replacementPublish.published, "matching replacement publish should succeed");
        passed &= Expect(replacementPublish.dataToRetire == firstData, "publish should transfer displaced data to the caller");

        const std::shared_ptr<const ve::FrameGraphDebugData> resetData = exchange.Reset();
        passed &= Expect(resetData == secondData, "reset should transfer exchange-held data to the caller");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Idle, "ownership-transfer reset should restore idle state");
        return passed;
    }

    bool TestMismatchedCaptureCompletionFailsAndPreservesPublishedSnapshot()
    {
        ve::FrameGraphDebugCaptureExchange exchange;
        bool passed = true;

        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "initial mismatch-test request should succeed");
        const auto firstRequest = exchange.ConsumeRequest();
        auto firstData = std::make_shared<ve::FrameGraphDebugData>();
        firstData->requestId = firstRequest ? firstRequest->requestId : 0;
        (void)exchange.Publish(firstData);

        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "replacement mismatch-test request should succeed");
        const auto secondRequest = exchange.ConsumeRequest();
        auto mismatchedData = std::make_shared<ve::FrameGraphDebugData>();
        mismatchedData->requestId = firstData->requestId;

        const ve::FrameGraphDebugCapturePublishResult completion =
            ve::CompleteFrameGraphDebugCapture(exchange, ve::ErrorCode::None, secondRequest.value_or(ve::FrameGraphDebugCaptureRequest{}), mismatchedData, {});
        passed &= Expect(!completion.published, "mismatched capture data must not publish");
        passed &= Expect(completion.dataToRetire == mismatchedData, "mismatched capture data should be returned for render-thread retirement");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Failed, "mismatched capture data should fail the active request");
        passed &= Expect(exchange.GetFailureMessage().find("request ID mismatch") != std::string::npos,
                         "mismatched capture failure should identify the request ID mismatch");
        passed &= Expect(exchange.TakePublishedData() == firstData, "mismatched capture should preserve the prior published snapshot");
        return passed;
    }

    bool TestCaptureFailurePreservesPublishedSnapshot()
    {
        ve::FrameGraphDebugCaptureExchange exchange;
        bool passed = true;

        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "snapshot request should succeed");
        const auto firstRequest = exchange.ConsumeRequest();
        auto firstData = std::make_shared<ve::FrameGraphDebugData>();
        firstData->requestId = firstRequest ? firstRequest->requestId : 0;
        (void)exchange.Publish(firstData);

        passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "capture after a successful snapshot should succeed");
        const auto failedRequest = exchange.ConsumeRequest();
        exchange.Fail(failedRequest ? failedRequest->requestId : 0, "capture failed after submit");
        passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Failed, "failed replacement capture should report failure");
        passed &= Expect(exchange.TakePublishedData() == firstData, "failed replacement capture should preserve the prior snapshot");
        return passed;
    }

    bool TestRenderSystemFrameGraphDebugLifecycleAdmission()
    {
        ve::SetExpectedSceneThreadId(ve::GetCurrentThreadId());
        ve::RenderSystem renderSystem;
        bool passed = true;

        passed &= Expect(renderSystem.RequestFrameGraphDebugCapture(0.5F) == ve::ErrorCode::InvalidState,
                         "stopped RenderSystem should reject frame graph capture requests");

        auto rejectedData = std::make_shared<ve::FrameGraphDebugData>();
        const ve::FrameGraphDebugData* rejectedIdentity = rejectedData.get();
        std::shared_ptr<const ve::FrameGraphDebugData> returnedData = renderSystem.RetireFrameGraphDebugData(std::move(rejectedData));
        passed &=
            Expect(!rejectedData && returnedData.get() == rejectedIdentity, "stopped RenderSystem should return rejected retirement ownership to the caller");

        passed &= Expect(renderSystem.Initialize({}) == ve::ErrorCode::None, "RenderSystem should initialize for lifecycle admission coverage");
        passed &=
            Expect(renderSystem.RequestFrameGraphDebugCapture(0.5F) == ve::ErrorCode::None, "running RenderSystem should accept a frame graph capture request");

        auto acceptedData = std::make_shared<ve::FrameGraphDebugData>();
        passed &= Expect(!renderSystem.RetireFrameGraphDebugData(std::move(acceptedData)),
                         "running RenderSystem should accept frame graph debug retirement ownership");
        renderSystem.Shutdown();

        passed &= Expect(renderSystem.GetFrameGraphDebugCaptureStatus() == ve::FrameGraphDebugCaptureStatus::Idle,
                         "RenderSystem shutdown should reset an admitted capture request");
        passed &= Expect(renderSystem.RequestFrameGraphDebugCapture(0.5F) == ve::ErrorCode::InvalidState,
                         "shutdown RenderSystem should reject stale frame graph capture requests");
        return passed;
    }

    bool TestPanelCaptureRules()
    {
        bool passed = true;
        passed &= Expect(!ve::editor::CanCaptureFrameGraph(false, ve::FrameGraphDebugCaptureStatus::Idle), "Editing Mode must disable frame graph capture");
        passed &= Expect(ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Idle), "Play Mode must enable an idle frame graph capture");
        passed &= Expect(!ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Armed), "an armed capture must disable another request");
        passed &=
            Expect(!ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Capturing), "a capturing request must disable another request");
        passed &= Expect(ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Ready), "a ready snapshot must allow its replacement");
        passed &= Expect(!ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Ready, true),
                         "an unconsumed ready snapshot must serialize the panel handoff before another request");
        passed &= Expect(!ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Idle, true),
                         "a pending panel handoff must disable capture independently of exchange status");
        passed &= Expect(!ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Failed, true),
                         "a pending panel handoff must fail closed until PollCapture observes its failure");
        passed &= Expect(ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Failed), "a failed capture must allow retry");
        passed &=
            Expect(!ve::editor::CanCaptureFrameGraph(true, static_cast<ve::FrameGraphDebugCaptureStatus>(255)), "unknown capture states must fail closed");
        passed &=
            Expect(ve::editor::ShouldPauseAfterFrameGraphCapture(true, false, ve::ErrorCode::None), "a successful request should pause a running Play session");
        passed &=
            Expect(!ve::editor::ShouldPauseAfterFrameGraphCapture(true, true, ve::ErrorCode::None), "an already paused Play session needs no pause toggle");
        passed &= Expect(!ve::editor::ShouldPauseAfterFrameGraphCapture(false, false, ve::ErrorCode::None),
                         "Editing Mode must not be paused by frame graph capture logic");
        passed &=
            Expect(!ve::editor::ShouldPauseAfterFrameGraphCapture(true, false, ve::ErrorCode::InvalidState), "a rejected request must not pause Play Mode");
        return passed;
    }

    bool TestPanelPinColumnsAndVisualRoles()
    {
        ve::FrameGraphDebugPass pass;
        pass.type = ve::FrameGraphDebugPassType::Compute;
        pass.accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, 3, 1, std::nullopt, 0, false});
        pass.accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, 7, 2, 3, 0, true});
        pass.accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, 7, 2, 3, 0, true});

        const std::optional<ve::editor::FrameGraphDebugPanelPins> pins = ve::editor::BuildFrameGraphDebugPanelPins(pass);
        bool passed = true;
        passed &= Expect(pins.has_value(), "valid pass accesses should produce panel pin columns");
        const bool hasExpectedPinCounts = pins.has_value() && pins->inputs.size() == 2 && pins->outputs.size() == 1;
        passed &= Expect(hasExpectedPinCounts, "panel pin columns should deduplicate inputs and outputs independently");
        if (!hasExpectedPinCounts)
        {
            return false;
        }
        passed &= Expect(pins.has_value() && pins->inputs[0] == ve::editor::FrameGraphDebugPanelPin{ve::FrameGraphDebugResourceKind::Texture, 3, 1},
                         "the first input pin should preserve texture access order and version");
        passed &= Expect(pins.has_value() && pins->inputs[1] == ve::editor::FrameGraphDebugPanelPin{ve::FrameGraphDebugResourceKind::Buffer, 7, 2},
                         "the buffer input pin should retain its consumed version");
        passed &= Expect(pins.has_value() && pins->outputs[0] == ve::editor::FrameGraphDebugPanelPin{ve::FrameGraphDebugResourceKind::Buffer, 7, 3},
                         "the produced buffer version should appear in the output column");

        using VisualRole = ve::editor::FrameGraphDebugPanelVisualRole;
        passed &= Expect(ve::editor::GetFrameGraphDebugPanelPassRole(pass) == std::optional<VisualRole>{VisualRole::ComputePass},
                         "a live compute pass should use the compute visual role");
        pass.culled = true;
        passed &= Expect(ve::editor::GetFrameGraphDebugPanelPassRole(pass) == std::optional<VisualRole>{VisualRole::CulledPass},
                         "the culled visual role should override the pass type");
        pass.culled = false;
        pass.type = ve::FrameGraphDebugPassType::Raster;
        passed &= Expect(ve::editor::GetFrameGraphDebugPanelPassRole(pass) == std::optional<VisualRole>{VisualRole::RasterPass},
                         "a live raster pass should use the raster visual role");

        passed &= Expect(
            ve::editor::GetFrameGraphDebugPanelDependencyRole(ve::FrameGraphDebugDependencyHazard::Raw) == std::optional<VisualRole>{VisualRole::RawLink} &&
                ve::editor::GetFrameGraphDebugPanelDependencyRole(ve::FrameGraphDebugDependencyHazard::War) == std::optional<VisualRole>{VisualRole::WarLink} &&
                ve::editor::GetFrameGraphDebugPanelDependencyRole(ve::FrameGraphDebugDependencyHazard::Waw) == std::optional<VisualRole>{VisualRole::WawLink},
            "RAW, WAR, and WAW hazards should use their matching link visual roles");
        passed &= Expect(
            ve::editor::GetFrameGraphDebugPanelResourceRole(ve::FrameGraphDebugResourceKind::Texture) == std::optional<VisualRole>{VisualRole::TexturePin} &&
                ve::editor::GetFrameGraphDebugPanelResourceRole(ve::FrameGraphDebugResourceKind::Buffer) == std::optional<VisualRole>{VisualRole::BufferPin},
            "texture and buffer resources should use their matching pin visual roles");

        ve::FrameGraphDebugPass malformedPass = pass;
        malformedPass.accesses[0].resourceIndex = ve::InvalidFrameGraphResourceIndex;
        passed &= Expect(!ve::editor::BuildFrameGraphDebugPanelPins(malformedPass).has_value(), "invalid resource sentinels should reject panel pin columns");
        malformedPass = pass;
        malformedPass.accesses[0].resourceKind = static_cast<ve::FrameGraphDebugResourceKind>(255);
        passed &= Expect(!ve::editor::BuildFrameGraphDebugPanelPins(malformedPass).has_value(), "unknown resource kinds should reject panel pin columns");
        malformedPass.culled = false;
        malformedPass.type = static_cast<ve::FrameGraphDebugPassType>(255);
        passed &= Expect(!ve::editor::GetFrameGraphDebugPanelPassRole(malformedPass).has_value() &&
                             !ve::editor::GetFrameGraphDebugPanelDependencyRole(static_cast<ve::FrameGraphDebugDependencyHazard>(255)).has_value() &&
                             !ve::editor::GetFrameGraphDebugPanelResourceRole(static_cast<ve::FrameGraphDebugResourceKind>(255)).has_value(),
                         "unknown panel classifications should fail closed");
        return passed;
    }

    ve::Float32 MeasurePanelTestText(std::string_view text)
    {
        return static_cast<ve::Float32>(text.size());
    }

    bool TestPanelDisplayLabelsElideLongNamesWithoutMovingPinSemantics()
    {
        using DisplayLabel = ve::editor::FrameGraphDebugPanelDisplayLabel;

        const DisplayLabel shortLabel = ve::editor::BuildFrameGraphDebugPanelDisplayLabel("<- ", "Color", " v1", 32.0F, &MeasurePanelTestText);
        bool passed = Expect(!shortLabel.elided && shortLabel.visible == "<- Color v1" && shortLabel.full == shortLabel.visible,
                             "short panel labels should remain unchanged");

        const DisplayLabel inputLabel =
            ve::editor::BuildFrameGraphDebugPanelDisplayLabel("<- ", "VirtualShadowRequestList", " v2", 18.0F, &MeasurePanelTestText);
        passed &= Expect(inputLabel.elided && inputLabel.visible.size() <= 18 && inputLabel.visible.starts_with("<- ") &&
                             inputLabel.visible.ends_with("... v2") && inputLabel.full == "<- VirtualShadowRequestList v2",
                         "long input labels should preserve direction and version while fitting the pin column");

        const DisplayLabel outputLabel =
            ve::editor::BuildFrameGraphDebugPanelDisplayLabel("", "VirtualShadowPhysicalPages", " v3 ->", 20.0F, &MeasurePanelTestText);
        passed &= Expect(outputLabel.elided && outputLabel.visible.size() <= 20 && outputLabel.visible.ends_with("... v3 ->") &&
                             outputLabel.full == "VirtualShadowPhysicalPages v3 ->",
                         "long output labels should preserve their version and output direction while fitting the pin column");
        return passed;
    }

    bool TestPanelTooltipDefersElidedLabelText()
    {
        const ve::editor::FrameGraphDebugPanelDisplayLabel elided =
            ve::editor::BuildFrameGraphDebugPanelDisplayLabel("<- ", "VirtualShadowRequestList", " v2", 18.0F, &MeasurePanelTestText);
        const ve::editor::FrameGraphDebugPanelDisplayLabel complete =
            ve::editor::BuildFrameGraphDebugPanelDisplayLabel("<- ", "Color", " v1", 32.0F, &MeasurePanelTestText);

        const std::optional<std::string> hoveredTooltip = ve::editor::BuildFrameGraphDebugPanelTooltipText(elided, true);
        bool passed = Expect(hoveredTooltip == std::optional<std::string>{elided.full},
                             "an hovered elided node label should defer its full text for a screen-space tooltip");
        passed &= Expect(!ve::editor::BuildFrameGraphDebugPanelTooltipText(elided, false).has_value(),
                         "an elided node label should not queue a tooltip while it is not hovered");
        passed &= Expect(!ve::editor::BuildFrameGraphDebugPanelTooltipText(complete, true).has_value(),
                         "a complete node label should not queue a redundant tooltip");
        return passed;
    }

    bool TestPanelColumnLayoutReservesFullHeightPreview()
    {
        const ve::editor::FrameGraphDebugPanelColumnLayout regular = ve::editor::CalculateFrameGraphDebugPanelColumnLayout(1000.0F, 8.0F);
        const ve::editor::FrameGraphDebugPanelColumnLayout narrow = ve::editor::CalculateFrameGraphDebugPanelColumnLayout(400.0F, 8.0F);

        bool passed = true;
        passed &= Expect(std::abs(regular.leftWidth + regular.rightWidth + 8.0F - 1000.0F) < 0.01F,
                         "frame graph panel columns should consume the full available width");
        passed &= Expect(regular.leftWidth > regular.rightWidth && std::abs(regular.leftWidth / 992.0F - 0.55F) < 0.01F,
                         "frame graph panel should preserve the existing 55/45 left-to-preview width balance");
        passed &= Expect(narrow.leftWidth > 0.0F && narrow.rightWidth > 0.0F &&
                             std::abs(narrow.leftWidth + narrow.rightWidth + 8.0F - 400.0F) < 0.01F,
                         "narrow frame graph panels should retain both columns without overflowing");
        return passed;
    }

    bool TestPanelDependencyLayoutFlowsLeftToRight()
    {
        ve::FrameGraphDebugData data;
        data.passes.resize(6);
        for (ve::UInt32 passIndex = 0; passIndex < 5; ++passIndex)
        {
            data.passes[passIndex].registrationIndex = passIndex;
            data.passes[passIndex].compiledIndex = passIndex;
            data.passes[passIndex].retained = true;
        }
        data.passes[5].registrationIndex = 5;
        data.passes[5].culled = true;

        data.dependencies = {
            {0, 1, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw},
            {0, 2, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw},
            {1, 3, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw},
            {2, 3, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw},
        };

        const std::optional<std::vector<ve::editor::FrameGraphDebugPanelNodePosition>> layout = ve::editor::BuildFrameGraphDebugPanelLayout(data);
        const std::vector<ve::editor::FrameGraphDebugPanelNodePosition> expected = {
            {0, 0},
            {1, 0},
            {1, 1},
            {2, 0},
            {0, 1},
            {3, 0},
        };

        bool passed = true;
        passed &= Expect(layout.has_value(), "an acyclic frame graph should produce a dependency layout");
        passed &= Expect(layout.has_value() && layout->size() == data.passes.size(), "the dependency layout should contain one position per pass");
        passed &= Expect(layout.has_value() && *layout == expected,
                         "dependency columns should flow left-to-right while deterministic rows and culled passes remain stable");

        data.dependencies = {
            {0, 1, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw},
            {1, 0, ve::FrameGraphDebugResourceKind::Texture, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw},
        };
        passed &= Expect(!ve::editor::BuildFrameGraphDebugPanelLayout(data).has_value(), "an executable dependency cycle must fail closed");
        return passed;
    }

    bool TestPanelElementIdsAreStableCollisionFreeAndReversible()
    {
        constexpr ve::UInt32 MaxResourceIndex = std::numeric_limits<ve::UInt32>::max() - 1;
        constexpr ve::UInt32 MaxVersion = std::numeric_limits<ve::UInt32>::max();

        ve::FrameGraphDebugData data;
        data.passes.resize(2);
        data.passes[0].accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, MaxResourceIndex, MaxVersion, MaxVersion, 0, true});
        data.passes[0].accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, MaxResourceIndex, MaxVersion, MaxVersion, 0, true});
        data.passes[0].accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, MaxResourceIndex, MaxVersion, MaxVersion, 0, true});
        data.passes[1].accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, MaxResourceIndex, MaxVersion, std::nullopt, 0, false});
        data.passes[1].accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, MaxResourceIndex, MaxVersion, std::nullopt, 0, false});
        data.dependencies.push_back({0, 1, ve::FrameGraphDebugResourceKind::Texture, MaxResourceIndex, MaxVersion, ve::FrameGraphDebugDependencyHazard::Raw});
        data.dependencies.push_back({0, 1, ve::FrameGraphDebugResourceKind::Buffer, MaxResourceIndex, MaxVersion, ve::FrameGraphDebugDependencyHazard::Waw});

        ve::editor::FrameGraphDebugPanelIdRegistry registry(data);
        ve::editor::FrameGraphDebugPanelIdRegistry rebuiltRegistry(data);
        const std::vector<std::optional<ve::UInt64>> ids = {
            registry.FindPassNodeId(0),
            registry.FindPassNodeId(1),
            registry.FindTexturePinId(0, MaxResourceIndex, MaxVersion, true),
            registry.FindTexturePinId(1, MaxResourceIndex, MaxVersion, false),
            registry.FindBufferPinId(0, MaxResourceIndex, MaxVersion, true),
            registry.FindBufferPinId(1, MaxResourceIndex, MaxVersion, false),
            registry.FindDependencyLinkId(0),
            registry.FindDependencyLinkId(1),
        };

        bool passed = true;
        passed &= Expect(std::all_of(ids.begin(), ids.end(), [](const auto& id) { return id.has_value() && *id != 0; }),
                         "every simultaneously visible panel element should receive a non-zero ID");
        std::vector<ve::UInt64> concreteIds;
        for (const std::optional<ve::UInt64> id : ids)
        {
            if (id.has_value())
            {
                concreteIds.push_back(*id);
            }
        }
        std::sort(concreteIds.begin(), concreteIds.end());
        passed &= Expect(std::adjacent_find(concreteIds.begin(), concreteIds.end()) == concreteIds.end(),
                         "pass, texture pin, buffer pin, and dependency link IDs must not collide");
        passed &= Expect(registry.GetElementCount() >= ids.size(), "the registry should include all requested topology elements");

        for (const std::optional<ve::UInt64> id : ids)
        {
            if (!id.has_value())
            {
                continue;
            }
            const std::optional<ve::editor::FrameGraphDebugPanelElement> element = registry.FindElement(*id);
            passed &= Expect(element.has_value() && registry.FindId(*element) == id, "panel element IDs must round-trip through reverse lookup");
            passed &=
                Expect(element.has_value() && rebuiltRegistry.FindId(*element) == id, "rebuilding the same snapshot must produce the same deterministic ID");
        }

        passed &= Expect(!registry.FindPassNodeId(2).has_value(), "unknown pass nodes must not alias a registered ID");
        passed &= Expect(!registry.FindElement(0).has_value(), "zero must remain an invalid node-editor ID");
        passed &= Expect(!registry.FindElement(std::numeric_limits<ve::UInt64>::max()).has_value(), "out-of-range node-editor IDs must fail reverse lookup");

        std::reverse(data.passes[0].accesses.begin(), data.passes[0].accesses.end());
        passed &= Expect(registry.Reset(data), "a valid snapshot should reset the panel registry");
        passed &= Expect(registry.FindTexturePinId(0, MaxResourceIndex, MaxVersion, true) == ids[2],
                         "duplicate and permuted access insertion must not change stable IDs");
        registry.Clear();
        passed &= Expect(registry.GetElementCount() == 0 && !registry.FindPassNodeId(0).has_value(), "Clear must remove all registered IDs");

        data.passes[0].accesses[0].resourceIndex = ve::InvalidFrameGraphResourceIndex;
        passed &= Expect(!registry.Reset(data) && !registry.IsValid() && registry.GetElementCount() == 0,
                         "invalid resource sentinels must reject and clear the registry");
        data.passes[0].accesses[0].resourceIndex = MaxResourceIndex;
        data.passes[0].accesses[0].resourceKind = static_cast<ve::FrameGraphDebugResourceKind>(255);
        passed &= Expect(!registry.Reset(data) && registry.GetElementCount() == 0, "unknown resource kinds must reject and clear the registry");
        return passed;
    }

    bool TestPanelDependencyEndpointsUseRealHazardPins()
    {
        constexpr ve::UInt32 TextureIndex = 3;
        constexpr ve::UInt32 BufferIndex = 7;
        constexpr ve::UInt32 Version = 1;

        ve::FrameGraphDebugData data;
        data.passes.resize(3);
        data.passes[0].accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, TextureIndex, 0, Version, 0, true});
        data.passes[0].accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, BufferIndex, 0, Version, 0, true});
        data.passes[1].accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, TextureIndex, Version, std::nullopt, 0, false});
        data.passes[1].accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, BufferIndex, Version, std::nullopt, 0, false});
        data.passes[2].accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, TextureIndex, Version, Version + 1, 0, true});
        data.passes[2].accesses.push_back({ve::FrameGraphDebugResourceKind::Buffer, BufferIndex, Version, Version + 1, 0, true});

        for (const ve::FrameGraphDebugResourceKind resourceKind : {ve::FrameGraphDebugResourceKind::Texture, ve::FrameGraphDebugResourceKind::Buffer})
        {
            const ve::UInt32 resourceIndex = resourceKind == ve::FrameGraphDebugResourceKind::Texture ? TextureIndex : BufferIndex;
            data.dependencies.push_back({0, 1, resourceKind, resourceIndex, Version, ve::FrameGraphDebugDependencyHazard::Raw});
            data.dependencies.push_back({1, 2, resourceKind, resourceIndex, Version, ve::FrameGraphDebugDependencyHazard::War});
            data.dependencies.push_back({0, 2, resourceKind, resourceIndex, Version, ve::FrameGraphDebugDependencyHazard::Waw});
        }

        ve::editor::FrameGraphDebugPanelIdRegistry registry(data);
        bool passed = true;
        passed &= Expect(registry.IsValid(), "well-formed dependency accesses should build a valid registry");

        for (ve::UInt32 dependencyIndex = 0; dependencyIndex < data.dependencies.size(); ++dependencyIndex)
        {
            const ve::FrameGraphDebugDependency& dependency = data.dependencies[dependencyIndex];
            const bool texture = dependency.resourceKind == ve::FrameGraphDebugResourceKind::Texture;
            const auto FindPin = [&](ve::UInt32 passIndex, bool output)
            {
                return texture ? registry.FindTexturePinId(passIndex, dependency.resourceIndex, dependency.version, output)
                               : registry.FindBufferPinId(passIndex, dependency.resourceIndex, dependency.version, output);
            };

            const bool expectedStartOutput = dependency.hazard != ve::FrameGraphDebugDependencyHazard::War;
            const std::optional<ve::UInt64> expectedStart = FindPin(dependency.beforePass, expectedStartOutput);
            const std::optional<ve::UInt64> expectedEnd = FindPin(dependency.afterPass, false);
            const std::optional<ve::editor::FrameGraphDebugPanelDependencyEndpointIds> endpoints = registry.FindDependencyEndpointIds(dependencyIndex);
            passed &= Expect(registry.FindDependencyLinkId(dependencyIndex).has_value(), "a dependency with live access pins should receive a link ID");
            passed &= Expect(expectedStart.has_value() && expectedEnd.has_value() && endpoints.has_value() && endpoints->startId == *expectedStart &&
                                 endpoints->endId == *expectedEnd,
                             "RAW/WAR/WAW links must resolve to their hazard-specific real access pins");
        }

        passed &= Expect(!registry.FindTexturePinId(1, TextureIndex, Version, true).has_value(), "a texture reader must not receive a synthetic output pin");
        passed &= Expect(!registry.FindBufferPinId(1, BufferIndex, Version, true).has_value(), "a buffer reader must not receive a synthetic output pin");

        ve::FrameGraphDebugData malformed = data;
        malformed.passes[1].accesses.clear();
        passed &= Expect(registry.Reset(malformed), "missing dependency endpoints should not invalidate otherwise well-formed snapshot metadata");
        passed &= Expect(!registry.FindDependencyLinkId(0).has_value() && !registry.FindDependencyEndpointIds(0).has_value(),
                         "dependencies without both real access pins must remain unresolved and unregistered");

        malformed = data;
        malformed.dependencies[0].beforePass = ve::InvalidFrameGraphDebugPassIndex;
        passed &= Expect(!registry.Reset(malformed) && !registry.IsValid() && registry.GetElementCount() == 0,
                         "invalid dependency pass sentinels must reject and clear the registry");
        malformed = data;
        malformed.dependencies[0].afterPass = static_cast<ve::UInt32>(malformed.passes.size());
        passed &= Expect(!registry.Reset(malformed) && !registry.IsValid() && registry.GetElementCount() == 0,
                         "out-of-range dependency pass indices must reject the whole panel snapshot");
        passed &= Expect(!registry.FindDependencyEndpointIds(std::numeric_limits<ve::UInt32>::max()).has_value(),
                         "out-of-range dependencies must not resolve endpoint IDs");
        return passed;
    }

    bool DependenciesMatch(const std::vector<ve::FrameGraphDebugDependency>& actual, const std::vector<ve::FrameGraphDebugDependency>& expected)
    {
        if (actual.size() != expected.size())
        {
            return false;
        }

        for (ve::SizeT index = 0; index < actual.size(); ++index)
        {
            const ve::FrameGraphDebugDependency& actualDependency = actual[index];
            const ve::FrameGraphDebugDependency& expectedDependency = expected[index];
            if (actualDependency.resourceKind != expectedDependency.resourceKind || actualDependency.resourceIndex != expectedDependency.resourceIndex ||
                actualDependency.version != expectedDependency.version || actualDependency.hazard != expectedDependency.hazard ||
                actualDependency.beforePass != expectedDependency.beforePass || actualDependency.afterPass != expectedDependency.afterPass)
            {
                return false;
            }
        }

        return true;
    }

    bool CapturePlanEntryMatches(const ve::FrameGraphDebugCapturePlanEntry& actual, const ve::FrameGraphDebugCapturePlanEntry& expected)
    {
        return actual.textureIndex == expected.textureIndex && actual.version == expected.version &&
               actual.previewExtent.width == expected.previewExtent.width && actual.previewExtent.height == expected.previewExtent.height &&
               actual.swapchain == expected.swapchain && actual.requiresSampleableStaging == expected.requiresSampleableStaging &&
               actual.captureBeforePass == expected.captureBeforePass;
    }

    bool TestBuildOriginalGraphSnapshotAndCapturePlan()
    {
        ve::FrameGraphDebugSourceGraph source;
        source.frameIndex = 42;
        source.passes.resize(9);
        for (ve::UInt32 passIndex = 0; passIndex < source.passes.size(); ++passIndex)
        {
            source.passes[passIndex].name = "Pass" + std::to_string(passIndex);
            source.passes[passIndex].registrationIndex = passIndex;
            source.passes[passIndex].retained = passIndex != 3;
        }
        source.passes[0].type = ve::FrameGraphDebugPassType::Compute;
        source.passes[0].sideEffect = true;
        source.passes[0].accesses.push_back({ve::FrameGraphDebugResourceKind::Texture, 2, 0, 1, 17, true});
        source.passes[0].attachments.push_back({2, 1, ve::rhi::RhiLoadAction::Clear, ve::rhi::RhiStoreAction::Store, false, false});
        source.passes[0].textureUavBarriers.push_back({2, 1});
        source.passes[0].bufferUavBarriers.push_back({0, 0});
        source.compiledPassOrder = {5, 0, 1, 6, 7, 2, 4};

        ve::FrameGraphDebugSourceTexture importedTexture;
        importedTexture.name = "Imported";
        importedTexture.desc.width = 100;
        importedTexture.desc.height = 50;
        importedTexture.desc.format = ve::rhi::RhiFormat::Rgba8Unorm;
        importedTexture.desc.usage = ve::rhi::RhiTextureUsage::Sampled;
        importedTexture.imported = true;
        importedTexture.versions.push_back({ve::InvalidFrameGraphDebugPassIndex, {}, ve::InvalidFrameGraphDebugPassIndex, true});
        source.textures.push_back(importedTexture);

        ve::FrameGraphDebugSourceTexture uninitializedTexture;
        uninitializedTexture.name = "Uninitialized";
        uninitializedTexture.desc.width = 64;
        uninitializedTexture.desc.height = 32;
        uninitializedTexture.versions.push_back({});
        uninitializedTexture.versions.push_back({});
        source.textures.push_back(uninitializedTexture);

        ve::FrameGraphDebugSourceTexture chainedTexture;
        chainedTexture.name = "Chained";
        chainedTexture.desc.width = 201;
        chainedTexture.desc.height = 99;
        chainedTexture.desc.format = ve::rhi::RhiFormat::R32Uint;
        chainedTexture.desc.usage = ve::rhi::RhiTextureUsage::RenderTarget;
        chainedTexture.versions.push_back({});
        chainedTexture.versions.push_back({0, {1, 1}, 2, true});
        chainedTexture.versions.push_back({2, {}, ve::InvalidFrameGraphDebugPassIndex, false});
        source.textures.push_back(chainedTexture);

        ve::FrameGraphDebugSourceTexture culledTexture;
        culledTexture.name = "CulledOutput";
        culledTexture.desc.width = 32;
        culledTexture.desc.height = 16;
        culledTexture.versions.push_back({});
        culledTexture.versions.push_back({3, {}, ve::InvalidFrameGraphDebugPassIndex, true});
        source.textures.push_back(culledTexture);

        ve::FrameGraphDebugSourceTexture swapchainTexture;
        swapchainTexture.name = "Swapchain";
        swapchainTexture.desc.width = 1280;
        swapchainTexture.desc.height = 720;
        swapchainTexture.desc.format = ve::rhi::RhiFormat::Bgra8Unorm;
        swapchainTexture.desc.usage = ve::rhi::RhiTextureUsage::RenderTarget;
        swapchainTexture.imported = true;
        swapchainTexture.swapchain = true;
        swapchainTexture.versions.push_back({});
        swapchainTexture.versions.push_back({4, {}, ve::InvalidFrameGraphDebugPassIndex, false});
        source.textures.push_back(swapchainTexture);

        ve::FrameGraphDebugSourceBuffer buffer;
        buffer.name = "Buffer";
        buffer.size = 4096;
        buffer.versions.push_back({5, {6, 6}, 7, true});
        buffer.versions.push_back({7, {}, ve::InvalidFrameGraphDebugPassIndex, false});
        buffer.versions.push_back({6, {6}, ve::InvalidFrameGraphDebugPassIndex, false});
        source.buffers.push_back(buffer);

        const ve::FrameGraphDebugBuildResult result = ve::BuildFrameGraphDebugData(source, {7, 0.5F});
        bool passed = true;
        passed &= Expect(result.data != nullptr, "snapshot builder should return data");
        if (!result.data)
        {
            return false;
        }

        passed &= Expect(result.data->requestId == 7 && result.data->frameIndex == 42 && result.data->previewScale == 0.5F,
                         "snapshot should preserve capture and frame metadata");
        passed &= Expect(result.data->passes.size() == source.passes.size(), "snapshot should retain every original pass including culled passes");
        const ve::FrameGraphDebugPass& copiedPass = result.data->passes[0];
        passed &= Expect(copiedPass.name == "Pass0" && copiedPass.type == ve::FrameGraphDebugPassType::Compute && copiedPass.sideEffect &&
                             copiedPass.accesses.size() == 1 && copiedPass.accesses[0].resourceKind == ve::FrameGraphDebugResourceKind::Texture &&
                             copiedPass.accesses[0].resourceIndex == 2 && copiedPass.accesses[0].inputVersion == 0 &&
                             copiedPass.accesses[0].outputVersion == 1 && copiedPass.accesses[0].accessValue == 17 && copiedPass.accesses[0].write &&
                             copiedPass.attachments.size() == 1 && copiedPass.attachments[0].textureIndex == 2 && copiedPass.attachments[0].version == 1 &&
                             copiedPass.attachments[0].loadAction == ve::rhi::RhiLoadAction::Clear &&
                             copiedPass.attachments[0].storeAction == ve::rhi::RhiStoreAction::Store && !copiedPass.attachments[0].depth &&
                             !copiedPass.attachments[0].readOnly && copiedPass.textureUavBarriers == source.passes[0].textureUavBarriers &&
                             copiedPass.bufferUavBarriers == source.passes[0].bufferUavBarriers,
                         "snapshot should copy all original pass metadata");
        passed &= Expect(result.data->passes[5].compiledIndex == 0 && result.data->passes[0].compiledIndex == 1 && result.data->passes[2].compiledIndex == 5,
                         "compiled indices should map registration indices through original execution order");
        passed &= Expect(!result.data->passes[3].compiledIndex && result.data->passes[3].culled && !result.data->passes[3].retained,
                         "culled pass should remain present without a compiled index");
        passed &= Expect(!result.data->passes[8].compiledIndex && result.data->passes[8].culled && result.data->passes[8].retained,
                         "retained passes absent from compiled order should be reported as culled");
        passed &= Expect(result.data->executionPassIndices == source.compiledPassOrder,
                         "published execution order should contain only original compiled pass indices");

        const std::vector<ve::FrameGraphDebugDependency> expectedDependencies = {
            {0, 1, ve::FrameGraphDebugResourceKind::Texture, 2, 1, ve::FrameGraphDebugDependencyHazard::Raw},
            {1, 2, ve::FrameGraphDebugResourceKind::Texture, 2, 1, ve::FrameGraphDebugDependencyHazard::War},
            {0, 2, ve::FrameGraphDebugResourceKind::Texture, 2, 1, ve::FrameGraphDebugDependencyHazard::Waw},
            {5, 6, ve::FrameGraphDebugResourceKind::Buffer, 0, 0, ve::FrameGraphDebugDependencyHazard::Raw},
            {6, 7, ve::FrameGraphDebugResourceKind::Buffer, 0, 0, ve::FrameGraphDebugDependencyHazard::War},
            {5, 7, ve::FrameGraphDebugResourceKind::Buffer, 0, 0, ve::FrameGraphDebugDependencyHazard::Waw},
        };
        passed &= Expect(DependenciesMatch(result.data->dependencies, expectedDependencies),
                         "dependencies should be deduplicated and sorted by resource, version, hazard, and pass indices");

        const ve::FrameGraphDebugResourceVersion& chainedVersion1 = result.data->textures[2].versions[1];
        passed &= Expect(chainedVersion1.producer == 0 && chainedVersion1.readers == std::vector<ve::UInt32>({1, 1}) && chainedVersion1.firstCompiledUse == 1 &&
                             chainedVersion1.lastCompiledUse == 5 && chainedVersion1.exported,
                         "texture versions should preserve topology and calculate retained compiled lifetime");
        passed &= Expect(result.data->textures[3].versions[1].firstCompiledUse == std::nullopt &&
                             result.data->textures[3].versions[1].lastCompiledUse == std::nullopt,
                         "culled-only versions should not have compiled lifetime bounds");
        passed &= Expect(result.data->buffers[0].name == "Buffer" && result.data->buffers[0].size == 4096 &&
                             result.data->buffers[0].versions[0].firstCompiledUse == 0 && result.data->buffers[0].versions[0].lastCompiledUse == 4 &&
                             result.data->buffers[0].versions[0].exported && result.data->buffers[0].versions.size() == 3,
                         "buffer versions should remain metadata-only with retained compiled lifetime and skip self dependencies");
        passed &= Expect(result.data->textures[0].name == "Imported" && result.data->textures[0].desc == importedTexture.desc &&
                             result.data->textures[0].imported && !result.data->textures[0].swapchain && result.data->textures[4].swapchain,
                         "snapshot should copy texture metadata");

        passed &= Expect(result.capturePlan.size() == 4, "only initialized imports and retained produced texture versions should be planned");
        if (result.capturePlan.size() == 4)
        {
            passed &= Expect(CapturePlanEntryMatches(result.capturePlan[0], {0, 0, {50, 25}, false, false, ve::InvalidFrameGraphDebugPassIndex}),
                             "sampleable imported version zero should have the exact capture plan");
            passed &= Expect(CapturePlanEntryMatches(result.capturePlan[1], {2, 1, {101, 50}, false, true, 2}),
                             "first produced chained version should have the exact capture plan");
            passed &= Expect(CapturePlanEntryMatches(result.capturePlan[2], {2, 2, {101, 50}, false, true, ve::InvalidFrameGraphDebugPassIndex}),
                             "final produced chained version should have the exact capture plan");
            passed &= Expect(CapturePlanEntryMatches(result.capturePlan[3], {4, 1, {640, 360}, true, true, ve::InvalidFrameGraphDebugPassIndex}),
                             "retained produced swapchain version should have the exact capture plan");
        }

        passed &= Expect(result.data->textures[0].versions[0].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             result.data->textures[0].versions[0].preview.message == "capture pending" &&
                             result.data->textures[0].versions[0].preview.sourceExtent.width == 100 &&
                             result.data->textures[0].versions[0].preview.previewExtent.width == 50 &&
                             result.data->textures[0].versions[0].preview.sourceFormat == ve::rhi::RhiFormat::Rgba8Unorm,
                         "planned preview metadata should begin pending with source properties");
        passed &= Expect(result.data->textures[1].versions[0].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             result.data->textures[1].versions[0].preview.message == "uninitialized" &&
                             result.data->textures[1].versions[1].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             result.data->textures[1].versions[1].preview.message == "uninitialized" &&
                             result.data->textures[2].versions[0].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             result.data->textures[2].versions[0].preview.message == "uninitialized" &&
                             result.data->textures[3].versions[0].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             result.data->textures[3].versions[0].preview.message == "uninitialized",
                         "transient and unproduced versions should remain unavailable and report uninitialized");
        passed &= Expect(result.data->textures[3].versions[1].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             result.data->textures[3].versions[1].preview.message == "producer culled",
                         "culled output should remain unavailable instead of reviving its producer");
        passed &= Expect(result.data->textures[4].versions[0].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             result.data->textures[4].versions[0].preview.message == "swapchain contents undefined",
                         "swapchain version zero should remain undefined and unplanned");
        passed &= Expect(result.data->textures[0].versions[0].exported && result.data->textures[2].versions[1].exported &&
                             result.data->textures[3].versions[1].exported && !result.data->textures[2].versions[2].exported,
                         "exported flags should be copied for every resource version");
        passed &=
            Expect(result.data->passes.size() == 9 && result.data->executionPassIndices.size() == 7, "snapshot should contain no hidden or internal passes");
        return passed;
    }

    bool TestCapturePlanSkipsSamePassIntermediateVersion()
    {
        ve::FrameGraphDebugSourceGraph source;
        source.passes.resize(1);
        source.passes[0].name = "DoubleWriter";
        source.passes[0].registrationIndex = 0;
        source.passes[0].retained = true;
        source.compiledPassOrder = {0};

        ve::FrameGraphDebugSourceTexture texture;
        texture.name = "SamePassVersions";
        texture.desc.width = 32;
        texture.desc.height = 16;
        texture.desc.format = ve::rhi::RhiFormat::Rgba8Unorm;
        texture.desc.usage = ve::rhi::RhiTextureUsage::Sampled;
        texture.imported = true;
        texture.versions.push_back({ve::InvalidFrameGraphDebugPassIndex, {}, 0, false});
        texture.versions.push_back({0, {}, 0, false});
        texture.versions.push_back({0, {}, ve::InvalidFrameGraphDebugPassIndex, true});
        source.textures.push_back(texture);

        const ve::FrameGraphDebugBuildResult result = ve::BuildFrameGraphDebugData(source, {27, 0.5F});
        bool passed = true;
        passed &= Expect(result.capturePlan.size() == 2, "only versions with a pass boundary should receive capture entries");
        passed &= Expect(
            std::none_of(result.capturePlan.begin(), result.capturePlan.end(), [](const auto& entry) { return entry.textureIndex == 0 && entry.version == 1; }),
            "an intermediate version overwritten in its producer pass must not be captured");
        passed &= Expect(result.data->textures[0].versions[1].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             result.data->textures[0].versions[1].preview.message == "no pass boundary before next write",
                         "same-pass intermediate versions should explain why preview capture is unavailable");
        passed &= Expect(
            std::any_of(result.capturePlan.begin(), result.capturePlan.end(), [](const auto& entry) { return entry.textureIndex == 0 && entry.version == 2; }),
            "the final version written by the same pass should remain capturable");
        return passed;
    }

    bool TestFrameGraphDebugCaptureKeepsOriginalSnapshotAndOrdersHiddenReader()
    {
        PreviewTestDevice device;
        PreviewTestSwapchain swapchain;
        ve::FrameRenderPipelineData frameData;
        frameData.frameIndex = 91;
        frameData.device = &device;
        frameData.mainSwapchain = &swapchain;
        ve::RendererData rendererData;
        ve::FrameGraph frameGraph({frameData, rendererData});

        ve::FrameGraphTextureHandle versionOne;
        ve::FrameGraphTextureHandle versionTwo;
        ve::FrameGraphTextureHandle culledVersion;
        frameGraph.Setup(
            [&](ve::FrameGraph& graph)
            {
                ve::FrameGraphTextureDesc desc;
                desc.width = 128;
                desc.height = 64;
                desc.format = ve::rhi::RhiFormat::Rgba8Unorm;
                desc.usage = static_cast<ve::rhi::RhiTextureUsage>(static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::Sampled) |
                                                                   static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::RenderTarget));

                const ve::FrameGraphTextureHandle chained = graph.CreateTexture("Chained", desc);
                graph.AddRasterPass<int>(
                    "Producer",
                    [&](ve::FrameGraphBuilder& builder, int&) { versionOne = builder.WriteColorAttachment(chained, ve::rhi::RhiLoadAction::Clear); },
                    [](const int&, ve::RenderPassContext&) {});
                graph.AddRasterPass<int>(
                    "Writer",
                    [&](ve::FrameGraphBuilder& builder, int&) { versionTwo = builder.WriteColorAttachment(versionOne, ve::rhi::RhiLoadAction::Load); },
                    [](const int&, ve::RenderPassContext&) {});
                graph.Export(versionTwo);

                graph.AddComputePass<int>(
                    "Independent", [](ve::FrameGraphBuilder& builder, int&) { builder.SetSideEffect(); }, [](const int&, ve::RenderPassContext&) {});

                const ve::FrameGraphTextureHandle culled = graph.CreateTexture("Culled", desc);
                graph.AddRasterPass<int>(
                    "CulledProducer",
                    [&](ve::FrameGraphBuilder& builder, int&) { culledVersion = builder.WriteColorAttachment(culled, ve::rhi::RhiLoadAction::Clear); },
                    [](const int&, ve::RenderPassContext&) {});
            });

        ve::FrameGraphDebugFrameCapture capture;
        capture.request = {13, 0.5F};
        const ve::Error prepareResult = frameGraph.PrepareDebugCapture(capture);
        bool passed = true;
        passed &= Expect(prepareResult.IsOk(), "debug capture preparation should compile and freeze the original graph");
        passed &= Expect(capture.data != nullptr && capture.data->passes.size() == 4, "debug snapshot should contain only the four original passes");
        passed &= Expect(capture.data != nullptr && capture.data->executionPassIndices == std::vector<ve::UInt32>({0, 1, 2}),
                         "debug snapshot should retain only the original compiled pass order");
        passed &= Expect(capture.data != nullptr && capture.data->passes[0].compiledIndex == 0 && capture.data->passes[1].compiledIndex == 1 &&
                             capture.data->passes[2].compiledIndex == 2,
                         "frozen pass compiled indices should match the frozen original execution order");
        passed &= Expect(capture.data != nullptr && capture.data->textures[1].versions[culledVersion.version].preview.message == "producer culled",
                         "culled output should remain unavailable without an injected reader");

        const ve::Error compileResult = frameGraph.Compile();
        passed &= Expect(compileResult.IsOk(), "instrumented graph should compile");
        const std::vector<ve::FrameGraphPassDiagnostics> diagnostics = frameGraph.GetPassDiagnostics();
        const auto producer = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& pass) { return pass.name == "Producer"; });
        const auto writer = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& pass) { return pass.name == "Writer"; });
        const auto independent = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& pass) { return pass.name == "Independent"; });
        const auto hiddenReader =
            std::find_if(diagnostics.begin(),
                         diagnostics.end(),
                         [versionOne](const auto& pass)
                         {
                             return pass.internal && std::any_of(pass.textureAccesses.begin(),
                                                                 pass.textureAccesses.end(),
                                                                 [versionOne](const auto& access) { return !access.write && access.input == versionOne; });
                         });
        passed &= Expect(producer != diagnostics.end() && writer != diagnostics.end() && hiddenReader != diagnostics.end(),
                         "instrumentation should add an explicitly internal reader for the exact logical version");
        passed &= Expect(producer != diagnostics.end() && writer != diagnostics.end() && hiddenReader != diagnostics.end() && producer->compiledIndex &&
                             hiddenReader->compiledIndex && writer->compiledIndex && *producer->compiledIndex < *hiddenReader->compiledIndex &&
                             *hiddenReader->compiledIndex < *writer->compiledIndex,
                         "version one must be captured before version two overwrites its physical backing");
        passed &= Expect(writer != diagnostics.end() && independent != diagnostics.end() && writer->compiledIndex && independent->compiledIndex &&
                             *independent->compiledIndex < *writer->compiledIndex && capture.data->executionPassIndices == std::vector<ve::UInt32>({0, 1, 2}) &&
                             capture.data->passes[1].compiledIndex == 1 && capture.data->passes[2].compiledIndex == 2,
                         "instrumented reordering must not mutate the original execution order or pass compiled indices");
        passed &= Expect(std::none_of(diagnostics.begin(),
                                      diagnostics.end(),
                                      [culledVersion](const auto& pass)
                                      {
                                          return pass.internal &&
                                                 std::any_of(pass.textureAccesses.begin(),
                                                             pass.textureAccesses.end(),
                                                             [culledVersion](const auto& access) { return !access.write && access.input == culledVersion; });
                                      }),
                         "culled output must not be revived by a hidden capture pass");

        std::vector<std::unique_ptr<ve::rhi::RhiObject>> retiredResources;
        if (capture.data != nullptr)
        {
            for (ve::FrameGraphDebugTexture& texture : capture.data->textures)
            {
                for (ve::FrameGraphDebugResourceVersion& version : texture.versions)
                {
                    if (version.preview.texture != nullptr)
                    {
                        version.preview.texture->Reset(retiredResources);
                    }
                }
            }
        }
        return passed;
    }

    bool TestPrepareDebugCaptureSkipsSamePassIntermediateVersionWithoutCycle()
    {
        PreviewTestDevice device;
        PreviewTestSwapchain swapchain;
        ve::rhi::RhiTextureDesc nativeDesc;
        nativeDesc.width = 64;
        nativeDesc.height = 32;
        nativeDesc.format = ve::rhi::RhiFormat::Rgba8Unorm;
        nativeDesc.usage = static_cast<ve::rhi::RhiTextureUsage>(static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::Sampled) |
                                                                 static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::Storage));
        PreviewTestTexture importedTexture(nativeDesc);

        ve::FrameRenderPipelineData frameData;
        frameData.frameIndex = 92;
        frameData.device = &device;
        frameData.mainSwapchain = &swapchain;
        ve::RendererData rendererData;
        ve::FrameGraph frameGraph({frameData, rendererData});

        ve::FrameGraphTextureHandle intermediate;
        ve::FrameGraphTextureHandle finalVersion;
        frameGraph.Setup(
            [&](ve::FrameGraph& graph)
            {
                ve::FrameGraphTextureDesc desc;
                desc.width = 64;
                desc.height = 32;
                desc.format = ve::rhi::RhiFormat::Rgba8Unorm;
                desc.usage = nativeDesc.usage;
                const ve::FrameGraphTextureHandle imported = graph.ImportTexture("DoubleWrite", desc, {&importedTexture, false});
                graph.AddComputePass<int>(
                    "DoubleWriter",
                    [&](ve::FrameGraphBuilder& builder, int&)
                    {
                        intermediate = builder.Write(imported);
                        finalVersion = builder.Write(intermediate);
                    },
                    [](const int&, ve::RenderPassContext&) {});
                graph.Export(finalVersion);
            });

        ve::FrameGraphDebugFrameCapture capture;
        capture.request = {28, 0.5F};
        const ve::Error prepareResult = frameGraph.PrepareDebugCapture(capture);
        const ve::Error compileResult = prepareResult.IsOk() ? frameGraph.Compile() : prepareResult;
        bool passed = true;
        passed &= Expect(prepareResult.IsOk() && compileResult.IsOk(), "same-pass intermediate preview skipping should keep the instrumented graph acyclic");
        passed &= Expect(capture.data != nullptr &&
                             capture.data->textures[0].versions[intermediate.version].preview.state == ve::FrameGraphDebugPreviewState::Unavailable &&
                             capture.data->textures[0].versions[intermediate.version].preview.message == "no pass boundary before next write",
                         "real graph capture should mark the same-pass intermediate version unavailable");
        passed &= Expect(capture.data != nullptr && capture.data->textures[0].versions[finalVersion.version].preview.texture != nullptr,
                         "real graph capture should still allocate a preview for the final same-pass version");

        std::vector<std::unique_ptr<ve::rhi::RhiObject>> retiredResources;
        if (capture.data != nullptr)
        {
            for (ve::FrameGraphDebugResourceVersion& version : capture.data->textures[0].versions)
            {
                if (version.preview.texture != nullptr)
                {
                    version.preview.texture->Reset(retiredResources);
                }
            }
        }
        return passed;
    }
} // namespace

int main()
{
    ve::SetExpectedRenderThreadId(ve::GetCurrentThreadId());
    if (TestPreviewScaleValidation() && TestPreviewExtent() && TestPreviewModeSelectionCoversEveryRhiFormat() && TestDepthPreviewContrastRemap() &&
        TestPreviewStagingSelectionUsesSampledFlagPresence() && TestPreviewTextureOwnerInitializationAndReset() && TestShaderManagerNonFatalCachePaths() &&
        TestCaptureRequestLifecycle() && TestCaptureRequestRejection() && TestMatchingPublishAndTake() &&
        TestInvalidPublishPreservesActiveCaptureAndPriorData() && TestCaptureFailureAndRecovery() && TestCaptureReset() && TestFrameGraphDebugSubmitGate() &&
        TestFrameGraphDebugPreviewCollectionDeduplicatesOwners() && TestCapturePublishAndResetTransferDisplacedOwnership() &&
        TestMismatchedCaptureCompletionFailsAndPreservesPublishedSnapshot() && TestCaptureFailurePreservesPublishedSnapshot() &&
        TestBuildOriginalGraphSnapshotAndCapturePlan() && TestFrameGraphDebugCaptureKeepsOriginalSnapshotAndOrdersHiddenReader() &&
        TestPrepareDebugCaptureSkipsSamePassIntermediateVersionWithoutCycle() && TestCapturePlanSkipsSamePassIntermediateVersion() &&
        TestRenderSystemFrameGraphDebugLifecycleAdmission() && TestPanelCaptureRules() && TestPanelPinColumnsAndVisualRoles() &&
        TestPanelDisplayLabelsElideLongNamesWithoutMovingPinSemantics() && TestPanelTooltipDefersElidedLabelText() &&
        TestPanelColumnLayoutReservesFullHeightPreview() && TestPanelDependencyLayoutFlowsLeftToRight() &&
        TestPanelElementIdsAreStableCollisionFreeAndReversible() && TestPanelDependencyEndpointsUseRealHazardPins())
    {
        std::cout << "VEngineFrameGraphDebugTests passed" << '\n';
        return 0;
    }

    return 1;
}
