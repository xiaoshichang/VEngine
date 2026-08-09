#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Render/ShaderArtifactLoader.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] Int32 PreviewModeVariant(FrameGraphDebugPreviewMode mode) noexcept
        {
            return static_cast<Int32>(mode);
        }

        [[nodiscard]] ErrorCode FailPreviewConversion(ErrorCode errorCode, const char* message)
        {
            VE_LOG_ERROR("Frame graph debug preview conversion failed: {}", message);
            return errorCode;
        }
    } // namespace

    FrameGraphDebugPreviewMode SelectFrameGraphDebugPreviewMode(rhi::RhiFormat format) noexcept
    {
        switch (format)
        {
        case rhi::RhiFormat::Rgba8Unorm:
        case rhi::RhiFormat::Bgra8Unorm:
        case rhi::RhiFormat::Rgb32Float:
            return FrameGraphDebugPreviewMode::Color;
        case rhi::RhiFormat::Depth32Float:
            return FrameGraphDebugPreviewMode::Depth;
        case rhi::RhiFormat::R32Uint:
            return FrameGraphDebugPreviewMode::UnsignedInteger;
        case rhi::RhiFormat::Unknown:
            return FrameGraphDebugPreviewMode::Unsupported;
        }

        return FrameGraphDebugPreviewMode::Unsupported;
    }

    bool NeedsFrameGraphDebugStaging(rhi::RhiTextureUsage usage) noexcept
    {
        return (static_cast<UInt32>(usage) & static_cast<UInt32>(rhi::RhiTextureUsage::Sampled)) == 0;
    }

    Float32 RemapFrameGraphDebugDepthForPreview(Float32 deviceDepth) noexcept
    {
        constexpr Float32 ContrastExponent = 64.0F;
        const Float32 saturatedDepth = std::clamp(deviceDepth, 0.0F, 1.0F);
        return 1.0F - std::pow(saturatedDepth, ContrastExponent);
    }

    FrameGraphDebugPreviewTexture::~FrameGraphDebugPreviewTexture()
    {
        if (texture_ != nullptr)
        {
            constexpr const char* message = "FrameGraphDebugPreviewTexture must be Reset on the Render Thread before its final destruction.";
            VE_LOG_FATAL("{}", message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message);
            std::terminate();
        }
    }

    ErrorCode FrameGraphDebugPreviewTexture::Initialize(rhi::RhiDevice& device, rhi::RhiExtent2D extent, std::string debugName)
    {
        VE_ASSERT_RENDER_THREAD();
        if (extent.width == 0 || extent.height == 0)
        {
            VE_LOG_ERROR("Frame graph debug preview texture '{}' has invalid zero extent {}x{}.", debugName, extent.width, extent.height);
            return ErrorCode::InvalidArgument;
        }
        if (texture_ != nullptr)
        {
            VE_LOG_ERROR("Frame graph debug preview texture '{}' is already initialized.", debugName);
            return ErrorCode::InvalidState;
        }

        rhi::RhiTextureDesc textureDesc = {};
        textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
        textureDesc.width = extent.width;
        textureDesc.height = extent.height;
        textureDesc.depth = 1;
        textureDesc.mipLevelCount = 1;
        textureDesc.format = rhi::RhiFormat::Rgba8Unorm;
        textureDesc.usage =
            static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::Sampled) | static_cast<UInt32>(rhi::RhiTextureUsage::RenderTarget));
        textureDesc.debugName = debugName.c_str();

        std::unique_ptr<rhi::RhiTexture> texture = device.CreateTexture(textureDesc);
        if (texture == nullptr)
        {
            VE_LOG_ERROR("Failed to allocate frame graph debug preview texture '{}': {}",
                         debugName,
                         device.GetLastErrorMessage() != nullptr ? device.GetLastErrorMessage() : "unknown RHI error");
            return ErrorCode::OutOfMemory;
        }

        texture_ = std::move(texture);
        nativeSampledViewHandle_.store(texture_->GetNativeSampledViewHandle(), std::memory_order_release);
        return ErrorCode::None;
    }

    rhi::RhiTexture* FrameGraphDebugPreviewTexture::GetTexture() noexcept
    {
        return texture_.get();
    }

    const rhi::RhiTexture* FrameGraphDebugPreviewTexture::GetTexture() const noexcept
    {
        return texture_.get();
    }

    void* FrameGraphDebugPreviewTexture::GetNativeSampledViewHandle() const noexcept
    {
        return nativeSampledViewHandle_.load(std::memory_order_acquire);
    }

    void FrameGraphDebugPreviewTexture::Reset(std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources)
    {
        VE_ASSERT_RENDER_THREAD();
        nativeSampledViewHandle_.store(nullptr, std::memory_order_release);
        if (texture_ != nullptr)
        {
            retiredResources.push_back(std::move(texture_));
        }
    }

    ErrorCode RecordFrameGraphDebugPreviewConversion(const rhi::RhiTexture& source, FrameGraphDebugPreviewMode mode, RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        if (mode == FrameGraphDebugPreviewMode::Unsupported)
        {
            return FailPreviewConversion(ErrorCode::Unsupported, "the source format has no preview conversion mode.");
        }
        if (source.GetDimension() != rhi::RhiTextureDimension::Texture2D || source.GetWidth() == 0 || source.GetHeight() == 0)
        {
            return FailPreviewConversion(ErrorCode::InvalidArgument, "the source must be a non-empty 2D texture.");
        }
        if (SelectFrameGraphDebugPreviewMode(source.GetFormat()) != mode)
        {
            return FailPreviewConversion(ErrorCode::InvalidArgument, "the requested conversion mode does not match the source format.");
        }
        if (context.executionInfo.colorAttachmentCount != 1 || context.executionInfo.colorFormat != rhi::RhiFormat::Rgba8Unorm ||
            context.executionInfo.depthEnabled)
        {
            return FailPreviewConversion(ErrorCode::InvalidState, "recording requires one Rgba8Unorm color attachment with depth disabled.");
        }

        ShaderManager* shaderManager = context.frameData.shaderManager;
        if (shaderManager == nullptr)
        {
            return FailPreviewConversion(ErrorCode::InvalidState, "the frame ShaderManager is unavailable.");
        }

        const bool metal = context.device.GetBackend() == rhi::RhiBackend::Metal;
        const std::string passName = mode == FrameGraphDebugPreviewMode::Color ? "Color" : mode == FrameGraphDebugPreviewMode::Depth ? "Depth" : "UnsignedInteger";
        rhi::RhiShaderModule* vertexShader = GetOrCompileShaderArtifact(*shaderManager, context.device,
                                                                           ShaderID{"FrameGraphDebugPreview.Vertex", metal ? 1 : 0},
                                                                           "FrameGraphDebugPreview", passName, rhi::RhiShaderStage::Vertex,
                                                                           "FrameGraphDebugPreviewVS");
        if (vertexShader == nullptr)
        {
            return FailPreviewConversion(ErrorCode::PlatformError, "the fullscreen vertex shader could not be compiled.");
        }

        rhi::RhiShaderModule* fragmentShader = GetOrCompileShaderArtifact(*shaderManager, context.device,
                                                                            ShaderID{"FrameGraphDebugPreview.Fragment", PreviewModeVariant(mode) | (metal ? (1 << 8) : 0)},
                                                                            "FrameGraphDebugPreview", passName, rhi::RhiShaderStage::Fragment,
                                                                            "FrameGraphDebugPreviewPS");
        if (fragmentShader == nullptr)
        {
            return FailPreviewConversion(ErrorCode::PlatformError, "the typed fragment shader could not be compiled.");
        }

        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Fragment, 0},
        };
        rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidNoCullRasterizer;
        pipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.boundShaderState.fragmentShader = fragmentShader;
        pipelineDesc.boundShaderState.vertexDeclaration = {};
        pipelineDesc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorAttachmentCount = 1;
        pipelineDesc.colorFormat = rhi::RhiFormat::Rgba8Unorm;
        pipelineDesc.depthFormat = rhi::RhiFormat::Unknown;
        pipelineDesc.debugName = "FrameGraphDebugPreviewPipeline";

        rhi::RhiPipelineState* pipeline = shaderManager->TryGetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{"FrameGraphDebugPreview.Pipeline", PreviewModeVariant(mode) | (metal ? (1 << 8) : 0)}, pipelineDesc);
        if (pipeline == nullptr)
        {
            return FailPreviewConversion(ErrorCode::PlatformError, "the fullscreen graphics pipeline could not be created.");
        }

        context.commandList.SetPipeline(*pipeline);
        context.commandList.SetTexture(rhi::RhiShaderStage::Fragment, 0, source);
        context.commandList.Draw(3, 0);
        return ErrorCode::None;
    }
} // namespace ve
