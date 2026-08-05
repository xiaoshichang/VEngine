#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/ShaderManager.h"
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
        inline constexpr const char* PreviewVertexHlsl = R"(
struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}
)";

        inline constexpr const char* PreviewColorFragmentHlsl = R"(
Texture2D<float4> SourceTexture : register(t0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

uint2 SourceCoordinate(float2 uv)
{
    uint width;
    uint height;
    SourceTexture.GetDimensions(width, height);
    const uint2 dimensions = uint2(width, height);
    return min(uint2(uv * float2(dimensions)), dimensions - 1u);
}

float4 PSColor(VSOutput input) : SV_TARGET
{
    return SourceTexture.Load(int3(SourceCoordinate(input.uv), 0));
}
)";

        inline constexpr const char* PreviewDepthFragmentHlsl = R"(
Texture2D<float> SourceTexture : register(t0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

uint2 SourceCoordinate(float2 uv)
{
    uint width;
    uint height;
    SourceTexture.GetDimensions(width, height);
    const uint2 dimensions = uint2(width, height);
    return min(uint2(uv * float2(dimensions)), dimensions - 1u);
}

float4 PSDepth(VSOutput input) : SV_TARGET
{
    const float depth = saturate(SourceTexture.Load(int3(SourceCoordinate(input.uv), 0)));
    const float visualDepth = 1.0f - pow(depth, 64.0f);
    return float4(visualDepth, visualDepth, visualDepth, 1.0f);
}
)";

        inline constexpr const char* PreviewUnsignedIntegerFragmentHlsl = R"(
Texture2D<uint> SourceTexture : register(t0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

uint2 SourceCoordinate(float2 uv)
{
    uint width;
    uint height;
    SourceTexture.GetDimensions(width, height);
    const uint2 dimensions = uint2(width, height);
    return min(uint2(uv * float2(dimensions)), dimensions - 1u);
}

uint HashInteger(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float4 PSUnsignedInteger(VSOutput input) : SV_TARGET
{
    const uint value = SourceTexture.Load(int3(SourceCoordinate(input.uv), 0));
    if (value == 0u)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const uint hash = HashInteger(value);
    const float3 color = float3(hash & 0xffu, (hash >> 8u) & 0xffu, (hash >> 16u) & 0xffu) / 255.0f;
    return float4(color, 1.0f);
}
)";

        inline constexpr const char* PreviewMetalSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
};

vertex VSOutput VSMain(uint vertexID [[vertex_id]])
{
    VSOutput output;
    output.uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

uint2 SourceCoordinate(float2 uv, uint width, uint height)
{
    const uint2 dimensions = uint2(width, height);
    return min(uint2(uv * float2(dimensions)), dimensions - 1u);
}

fragment float4 PSColor(VSOutput input [[stage_in]], texture2d<float, access::read> source [[texture(0)]])
{
    return source.read(SourceCoordinate(input.uv, source.get_width(), source.get_height()));
}

fragment float4 PSDepth(VSOutput input [[stage_in]], texture2d<float, access::read> source [[texture(0)]])
{
    const float depth = saturate(source.read(SourceCoordinate(input.uv, source.get_width(), source.get_height())).r);
    const float visualDepth = 1.0f - pow(depth, 64.0f);
    return float4(visualDepth, visualDepth, visualDepth, 1.0f);
}

uint HashInteger(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

fragment float4 PSUnsignedInteger(VSOutput input [[stage_in]], texture2d<uint, access::read> source [[texture(0)]])
{
    const uint value = source.read(SourceCoordinate(input.uv, source.get_width(), source.get_height())).r;
    if (value == 0u)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const uint hash = HashInteger(value);
    const float3 color = float3(hash & 0xffu, (hash >> 8u) & 0xffu, (hash >> 16u) & 0xffu) / 255.0f;
    return float4(color, 1.0f);
}
)";

        [[nodiscard]] Int32 PreviewModeVariant(FrameGraphDebugPreviewMode mode) noexcept
        {
            return static_cast<Int32>(mode);
        }

        [[nodiscard]] const char* PreviewFragmentEntryPoint(FrameGraphDebugPreviewMode mode) noexcept
        {
            switch (mode)
            {
            case FrameGraphDebugPreviewMode::Color:
                return "PSColor";
            case FrameGraphDebugPreviewMode::Depth:
                return "PSDepth";
            case FrameGraphDebugPreviewMode::UnsignedInteger:
                return "PSUnsignedInteger";
            case FrameGraphDebugPreviewMode::Unsupported:
                return nullptr;
            }
            return nullptr;
        }

        [[nodiscard]] const char* PreviewFragmentHlsl(FrameGraphDebugPreviewMode mode) noexcept
        {
            switch (mode)
            {
            case FrameGraphDebugPreviewMode::Color:
                return PreviewColorFragmentHlsl;
            case FrameGraphDebugPreviewMode::Depth:
                return PreviewDepthFragmentHlsl;
            case FrameGraphDebugPreviewMode::UnsignedInteger:
                return PreviewUnsignedIntegerFragmentHlsl;
            case FrameGraphDebugPreviewMode::Unsupported:
                return nullptr;
            }
            return nullptr;
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
        const char* fragmentEntryPoint = PreviewFragmentEntryPoint(mode);
        const char* fragmentSource = metal ? PreviewMetalSource : PreviewFragmentHlsl(mode);
        if (fragmentEntryPoint == nullptr || fragmentSource == nullptr)
        {
            return FailPreviewConversion(ErrorCode::Unsupported, "the conversion shader variant is unavailable.");
        }

        const rhi::RhiShaderModuleDesc vertexShaderDesc = {
            rhi::RhiShaderStage::Vertex,
            rhi::RhiShaderCodeFormat::Source,
            metal ? PreviewMetalSource : PreviewVertexHlsl,
            nullptr,
            0,
            "VSMain",
            "FrameGraphDebugPreviewVS",
        };
        rhi::RhiShaderModule* vertexShader =
            shaderManager->TryGetOrCompileShader(context.device, ShaderID{"FrameGraphDebugPreview.Vertex", metal ? 1 : 0}, vertexShaderDesc);
        if (vertexShader == nullptr)
        {
            return FailPreviewConversion(ErrorCode::PlatformError, "the fullscreen vertex shader could not be compiled.");
        }

        const rhi::RhiShaderModuleDesc fragmentShaderDesc = {
            rhi::RhiShaderStage::Fragment,
            rhi::RhiShaderCodeFormat::Source,
            fragmentSource,
            nullptr,
            0,
            fragmentEntryPoint,
            "FrameGraphDebugPreviewPS",
        };
        rhi::RhiShaderModule* fragmentShader = shaderManager->TryGetOrCompileShader(
            context.device, ShaderID{"FrameGraphDebugPreview.Fragment", PreviewModeVariant(mode) | (metal ? (1 << 8) : 0)}, fragmentShaderDesc);
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
