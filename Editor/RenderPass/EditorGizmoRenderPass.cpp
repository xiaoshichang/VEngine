#include "Editor/RenderPass/EditorGizmoRenderPass.h"

#include "Editor/Core/EditorBuiltinResources.h"
#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <iterator>
#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        inline constexpr const char* EditorGizmoLineVertexShaderName = "EditorGizmo.Line.Vertex";
        inline constexpr const char* EditorGizmoLineFragmentShaderName = "EditorGizmo.Line.Fragment";
        inline constexpr const char* EditorGizmoIconVertexShaderName = "EditorGizmo.Icon.Vertex";
        inline constexpr const char* EditorGizmoIconFragmentShaderName = "EditorGizmo.Icon.Fragment";
        const ShaderID EditorGizmoLineVertexShaderID{EditorGizmoLineVertexShaderName, 0};
        const ShaderID EditorGizmoLineFragmentShaderID{EditorGizmoLineFragmentShaderName, 0};
        const ShaderID EditorGizmoIconVertexShaderID{EditorGizmoIconVertexShaderName, 0};
        const ShaderID EditorGizmoIconFragmentShaderID{EditorGizmoIconFragmentShaderName, 0};

        const char* EditorGizmoLineShaderSource = R"(
cbuffer ViewConstants : register(b1)
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
};

struct VSInput
{
    float3 position : POSITION;
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 color : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0f));
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(saturate(input.color), 1.0f);
}
)";

        const char* EditorGizmoLineMetalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct ViewConstants
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
};

struct VSInput
{
    float3 position [[attribute(0)]];
    float3 color [[attribute(1)]];
};

struct VSOutput
{
    float4 position [[position]];
    float3 color;
};

[[vertex]] VSOutput VSMain(VSInput input [[stage_in]], constant ViewConstants* constants [[buffer(1)]])
{
    VSOutput output;
    output.position = constants->viewProjection * float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}

[[fragment]] float4 PSMain(VSOutput input [[stage_in]])
{
    return float4(clamp(input.color, 0.0f, 1.0f), 1.0f);
}
)";

        const char* EditorGizmoIconShaderSource = R"(
cbuffer ViewConstants : register(b1)
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
};

Texture2D IconAtlasTexture : register(t0);
SamplerState IconAtlasSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 uv : TEXCOORD0;
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 color : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0f));
    output.uv = input.uv.xy;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 atlas = IconAtlasTexture.Sample(IconAtlasSampler, input.uv);
    return float4(saturate(input.color), atlas.a);
}
)";

        const char* EditorGizmoIconMetalShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct ViewConstants
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
};

struct VSInput
{
    float3 position [[attribute(0)]];
    float3 uv [[attribute(1)]];
    float3 color [[attribute(2)]];
};

struct VSOutput
{
    float4 position [[position]];
    float2 uv;
    float3 color;
};

[[vertex]] VSOutput VSMain(VSInput input [[stage_in]], constant ViewConstants* constants [[buffer(1)]])
{
    VSOutput output;
    output.position = constants->viewProjection * float4(input.position, 1.0f);
    output.uv = input.uv.xy;
    output.color = input.color;
    return output;
}

[[fragment]] float4 PSMain(VSOutput input [[stage_in]], texture2d<float> iconAtlasTexture [[texture(0)]], sampler iconAtlasSampler [[sampler(0)]])
{
    float4 atlas = iconAtlasTexture.sample(iconAtlasSampler, input.uv);
    return float4(clamp(input.color, 0.0f, 1.0f), atlas.a);
}
)";

        [[nodiscard]] rhi::RhiBufferDesc MakeBufferDesc(UInt64 size, rhi::RhiBufferUsage usage, const void* initialData, const char* debugName) noexcept
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = usage;
            desc.initialData = initialData;
            desc.debugName = debugName;
            return desc;
        }

        [[nodiscard]] const char* SelectShaderSource(const rhi::RhiDevice& device, const char* hlslSource, const char* metalSource) noexcept
        {
            return device.GetBackend() == rhi::RhiBackend::Metal ? metalSource : hlslSource;
        }

        [[nodiscard]] std::string BuildDeviceFailureMessage(const rhi::RhiDevice& device, const char* message)
        {
            std::string result = message;
            const char* backendError = device.GetLastErrorMessage();
            if (backendError != nullptr && backendError[0] != '\0')
            {
                result += " Backend error: ";
                result += backendError;
            }

            return result;
        }

        [[nodiscard]] Int32 BuildEditorGizmoPipelineVariant(rhi::RhiFormat targetFormat) noexcept
        {
            return static_cast<Int32>(targetFormat);
        }

    } // namespace

    EditorGizmoRenderPass::EditorGizmoRenderPass(EditorGizmoRenderPassInitParam initParam)
        : initParam_(std::move(initParam))
    {
    }

    void EditorGizmoRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        struct GizmoPassData
        {
            FrameGraphTextureHandle color;
        };

        frameGraph.AddRasterPass<GizmoPassData>(
            "EditorGizmoPass",
            [&graphData](FrameGraphBuilder& builder, GizmoPassData& passData)
            {
                passData.color = builder.WriteColorAttachment(graphData.color, rhi::RhiLoadAction::Load);
                graphData.color = passData.color;
            },
            [this](const GizmoPassData&, RenderPassContext& context) { return Execute(context); });
    }

    ErrorCode EditorGizmoRenderPass::Execute(RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        if (initParam_.drawList == nullptr || (initParam_.drawList->lines.empty() && initParam_.drawList->icons.empty()))
        {
            return ErrorCode::None;
        }

        EnsurePipeline(context);
        if (linePipelineState_ == nullptr || iconPipelineState_ == nullptr)
        {
            return ErrorCode::InvalidState;
        }
        if (!EnsureIconResources(context))
        {
            return ErrorCode::InvalidState;
        }
        if (!UploadFrameResources(context))
        {
            return ErrorCode::InvalidState;
        }
        const rhi::RhiRenderArea& renderArea = context.executionInfo.renderArea;
        const UniformBufferAllocation viewUniform =
            context.frameData.GetViewUniform(context.rendererData.resolvedCamera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});

        rhi::RhiCommandList& commandList = context.commandList;
        if (uploadedIconVertexCount_ > 0)
        {
            commandList.SetPipeline(*iconPipelineState_);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
            commandList.SetTexture(rhi::RhiShaderStage::Fragment, 0, *iconAtlasTexture_);
            commandList.SetSampler(rhi::RhiShaderStage::Fragment, 0, *iconSampler_);
            commandList.SetVertexBuffer(0, *iconVertexBuffer_, sizeof(EditorGizmoIconVertex), 0);
            commandList.Draw(static_cast<UInt32>(uploadedIconVertexCount_), 0);
        }

        if (uploadedLineVertexCount_ > 0)
        {
            commandList.SetPipeline(*linePipelineState_);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
            commandList.SetVertexBuffer(0, *lineVertexBuffer_, sizeof(EditorGizmoVertex), 0);
            commandList.Draw(static_cast<UInt32>(uploadedLineVertexCount_), 0);
        }

        if (lineVertexBuffer_ != nullptr)
        {
            context.frameData.RetainTransientResource(std::move(lineVertexBuffer_));
        }
        if (iconVertexBuffer_ != nullptr)
        {
            context.frameData.RetainTransientResource(std::move(iconVertexBuffer_));
        }
        context.frameData.RetainTransientResource(std::move(iconAtlasTexture_));
        context.frameData.RetainTransientResource(std::move(iconSampler_));
        return ErrorCode::None;
    }

    void EditorGizmoRenderPass::EnsurePipeline(RenderPassContext& context)
    {
        const rhi::RhiFormat targetFormat = context.executionInfo.colorFormat;
        if (linePipelineState_ != nullptr && iconPipelineState_ != nullptr && pipelineColorFormat_ == targetFormat)
        {
            return;
        }

        ShaderManager* shaderManager = context.frameData.shaderManager;
        VE_ASSERT_MESSAGE(shaderManager != nullptr, "EditorGizmoRenderPass requires a ShaderManager.");

        rhi::RhiShaderModuleDesc lineVertexShaderDesc = {};
        lineVertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
        lineVertexShaderDesc.source = SelectShaderSource(context.device, EditorGizmoLineShaderSource, EditorGizmoLineMetalShaderSource);
        lineVertexShaderDesc.entryPoint = "VSMain";
        lineVertexShaderDesc.debugName = "EditorGizmoLineVertexShader";

        rhi::RhiShaderModule* lineVertexShader = shaderManager->GetOrCompileShader(context.device, EditorGizmoLineVertexShaderID, lineVertexShaderDesc);
        VE_ASSERT_MESSAGE(lineVertexShader != nullptr, "EditorGizmoRenderPass failed to get line vertex shader.");

        rhi::RhiShaderModuleDesc lineFragmentShaderDesc = {};
        lineFragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
        lineFragmentShaderDesc.source = SelectShaderSource(context.device, EditorGizmoLineShaderSource, EditorGizmoLineMetalShaderSource);
        lineFragmentShaderDesc.entryPoint = "PSMain";
        lineFragmentShaderDesc.debugName = "EditorGizmoLineFragmentShader";

        rhi::RhiShaderModule* lineFragmentShader = shaderManager->GetOrCompileShader(context.device, EditorGizmoLineFragmentShaderID, lineFragmentShaderDesc);
        VE_ASSERT_MESSAGE(lineFragmentShader != nullptr, "EditorGizmoRenderPass failed to get line fragment shader.");

        rhi::RhiShaderModuleDesc iconVertexShaderDesc = {};
        iconVertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
        iconVertexShaderDesc.source = SelectShaderSource(context.device, EditorGizmoIconShaderSource, EditorGizmoIconMetalShaderSource);
        iconVertexShaderDesc.entryPoint = "VSMain";
        iconVertexShaderDesc.debugName = "EditorGizmoIconVertexShader";

        rhi::RhiShaderModule* iconVertexShader = shaderManager->GetOrCompileShader(context.device, EditorGizmoIconVertexShaderID, iconVertexShaderDesc);
        VE_ASSERT_MESSAGE(iconVertexShader != nullptr, "EditorGizmoRenderPass failed to get icon vertex shader.");

        rhi::RhiShaderModuleDesc iconFragmentShaderDesc = {};
        iconFragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
        iconFragmentShaderDesc.source = SelectShaderSource(context.device, EditorGizmoIconShaderSource, EditorGizmoIconMetalShaderSource);
        iconFragmentShaderDesc.entryPoint = "PSMain";
        iconFragmentShaderDesc.debugName = "EditorGizmoIconFragmentShader";

        rhi::RhiShaderModule* iconFragmentShader = shaderManager->GetOrCompileShader(context.device, EditorGizmoIconFragmentShaderID, iconFragmentShaderDesc);
        VE_ASSERT_MESSAGE(iconFragmentShader != nullptr, "EditorGizmoRenderPass failed to get icon fragment shader.");

        rhi::RhiVertexAttributeDesc positionAttribute = {};
        positionAttribute.semanticName = "POSITION";
        positionAttribute.semanticIndex = 0;
        positionAttribute.format = rhi::RhiFormat::Rgb32Float;
        positionAttribute.offset = 0;

        rhi::RhiVertexAttributeDesc colorAttribute = {};
        colorAttribute.semanticName = "COLOR";
        colorAttribute.semanticIndex = 0;
        colorAttribute.format = rhi::RhiFormat::Rgb32Float;
        colorAttribute.offset = sizeof(Float32) * 3;

        const rhi::RhiVertexAttributeDesc vertexAttributes[] = {positionAttribute, colorAttribute};
        const rhi::RhiPipelineResourceBindingDesc lineResourceBindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
        };

        rhi::RhiRasterizerStateDesc lineRasterizer = rhi::StaticRenderStates::SolidNoCullRasterizer;
        lineRasterizer.antialiasedLineEnabled = true;

        rhi::RhiGraphicsPipelineDesc linePipelineDesc = {};
        linePipelineDesc.blendState = rhi::StaticRenderStates::AlphaBlend;
        linePipelineDesc.rasterizerState = lineRasterizer;
        linePipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
        linePipelineDesc.boundShaderState.vertexShader = lineVertexShader;
        linePipelineDesc.boundShaderState.fragmentShader = lineFragmentShader;
        linePipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        linePipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 2;
        linePipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(EditorGizmoVertex);
        linePipelineDesc.resourceLayout.bindings = lineResourceBindings;
        linePipelineDesc.resourceLayout.bindingCount = static_cast<UInt32>(std::size(lineResourceBindings));
        linePipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::LineList;
        linePipelineDesc.colorAttachmentCount = 1;
        linePipelineDesc.colorFormat = targetFormat;
        linePipelineDesc.debugName = "EditorGizmoLinePipeline";

        linePipelineState_ = shaderManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{"EditorGizmoLinePipeline", BuildEditorGizmoPipelineVariant(targetFormat)}, linePipelineDesc);
        if (linePipelineState_ == nullptr)
        {
            const std::string message = BuildDeviceFailureMessage(context.device, "EditorGizmoRenderPass failed to create line pipeline state.");
            VE_ASSERT_MESSAGE(linePipelineState_ != nullptr, message.c_str());
            return;
        }

        rhi::RhiVertexAttributeDesc iconPositionAttribute = {};
        iconPositionAttribute.semanticName = "POSITION";
        iconPositionAttribute.semanticIndex = 0;
        iconPositionAttribute.format = rhi::RhiFormat::Rgb32Float;
        iconPositionAttribute.offset = 0;

        rhi::RhiVertexAttributeDesc iconUvAttribute = {};
        iconUvAttribute.semanticName = "TEXCOORD";
        iconUvAttribute.semanticIndex = 0;
        iconUvAttribute.format = rhi::RhiFormat::Rgb32Float;
        iconUvAttribute.offset = sizeof(Float32) * 3;

        rhi::RhiVertexAttributeDesc iconColorAttribute = {};
        iconColorAttribute.semanticName = "COLOR";
        iconColorAttribute.semanticIndex = 0;
        iconColorAttribute.format = rhi::RhiFormat::Rgb32Float;
        iconColorAttribute.offset = sizeof(Float32) * 6;

        const rhi::RhiVertexAttributeDesc iconVertexAttributes[] = {iconPositionAttribute, iconUvAttribute, iconColorAttribute};
        const rhi::RhiPipelineResourceBindingDesc iconResourceBindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
            {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Fragment, 0},
            {rhi::RhiPipelineResourceKind::Sampler, rhi::RhiShaderStage::Fragment, 0},
        };

        rhi::RhiGraphicsPipelineDesc iconPipelineDesc = {};
        iconPipelineDesc.blendState = rhi::StaticRenderStates::AlphaBlend;
        iconPipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidNoCullRasterizer;
        iconPipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
        iconPipelineDesc.boundShaderState.vertexShader = iconVertexShader;
        iconPipelineDesc.boundShaderState.fragmentShader = iconFragmentShader;
        iconPipelineDesc.boundShaderState.vertexDeclaration.attributes = iconVertexAttributes;
        iconPipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 3;
        iconPipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(EditorGizmoIconVertex);
        iconPipelineDesc.resourceLayout.bindings = iconResourceBindings;
        iconPipelineDesc.resourceLayout.bindingCount = static_cast<UInt32>(std::size(iconResourceBindings));
        iconPipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        iconPipelineDesc.colorAttachmentCount = 1;
        iconPipelineDesc.colorFormat = targetFormat;
        iconPipelineDesc.debugName = "EditorGizmoIconPipeline";

        iconPipelineState_ = shaderManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{"EditorGizmoIconPipeline", BuildEditorGizmoPipelineVariant(targetFormat)}, iconPipelineDesc);
        if (iconPipelineState_ == nullptr)
        {
            const std::string message = BuildDeviceFailureMessage(context.device, "EditorGizmoRenderPass failed to create icon pipeline state.");
            VE_ASSERT_MESSAGE(iconPipelineState_ != nullptr, message.c_str());
            return;
        }
        pipelineColorFormat_ = targetFormat;
    }

    bool EditorGizmoRenderPass::EnsureIconResources(RenderPassContext& context)
    {
        if (iconAtlasTexture_ == nullptr)
        {
            const editor::BuiltinGizmoIconAtlas atlas = editor::GenerateBuiltinGizmoIconAtlas();
            rhi::RhiTextureDesc textureDesc = {};
            textureDesc.dimension = rhi::RhiTextureDimension::Texture2D;
            textureDesc.width = atlas.width;
            textureDesc.height = atlas.height;
            textureDesc.mipLevelCount = 1;
            textureDesc.format = rhi::RhiFormat::Rgba8Unorm;
            textureDesc.usage = rhi::RhiTextureUsage::Sampled;
            textureDesc.initialData = atlas.pixels.data();
            textureDesc.initialDataSize = atlas.pixels.size();
            textureDesc.initialDataRowPitch = atlas.rowPitch;
            textureDesc.debugName = "BuiltinGizmoIconAtlas";

            iconAtlasTexture_ = context.device.CreateTexture(textureDesc);
            if (iconAtlasTexture_ == nullptr)
            {
                const std::string message =
                    BuildDeviceFailureMessage(context.device, "EditorGizmoRenderPass failed to create builtin gizmo icon atlas texture.");
                VE_ASSERT_MESSAGE(iconAtlasTexture_ != nullptr, message.c_str());
                return false;
            }
        }

        if (iconSampler_ == nullptr)
        {
            iconSampler_ = context.device.CreateSampler(rhi::StaticRenderStates::BilinearClampSampler);
            if (iconSampler_ == nullptr)
            {
                const std::string message = BuildDeviceFailureMessage(context.device, "EditorGizmoRenderPass failed to create builtin gizmo icon sampler.");
                VE_ASSERT_MESSAGE(iconSampler_ != nullptr, message.c_str());
                return false;
            }
        }

        return true;
    }

    bool EditorGizmoRenderPass::UploadFrameResources(RenderPassContext& context)
    {
        VE_ASSERT(initParam_.drawList != nullptr);
        uploadedLineVertexCount_ = initParam_.drawList->lines.size();
        uploadedIconVertexCount_ = initParam_.drawList->icons.size();

        if (uploadedLineVertexCount_ > 0)
        {
            lineVertexBuffer_ = context.device.CreateBuffer(MakeBufferDesc(static_cast<UInt64>(uploadedLineVertexCount_ * sizeof(EditorGizmoVertex)),
                                                                           rhi::RhiBufferUsage::Vertex,
                                                                           initParam_.drawList->lines.data(),
                                                                           "EditorGizmoLineVertexBuffer"));
            if (lineVertexBuffer_ == nullptr)
            {
                const std::string message = BuildDeviceFailureMessage(context.device, "EditorGizmoRenderPass failed to create line vertex buffer.");
                VE_ASSERT_MESSAGE(lineVertexBuffer_ != nullptr, message.c_str());
                return false;
            }
        }
        else
        {
            lineVertexBuffer_.reset();
        }

        if (uploadedIconVertexCount_ > 0)
        {
            iconVertexBuffer_ = context.device.CreateBuffer(MakeBufferDesc(static_cast<UInt64>(uploadedIconVertexCount_ * sizeof(EditorGizmoIconVertex)),
                                                                           rhi::RhiBufferUsage::Vertex,
                                                                           initParam_.drawList->icons.data(),
                                                                           "EditorGizmoIconVertexBuffer"));
            if (iconVertexBuffer_ == nullptr)
            {
                const std::string message = BuildDeviceFailureMessage(context.device, "EditorGizmoRenderPass failed to create icon vertex buffer.");
                VE_ASSERT_MESSAGE(iconVertexBuffer_ != nullptr, message.c_str());
                return false;
            }
        }
        else
        {
            iconVertexBuffer_.reset();
        }
        return true;
    }

} // namespace ve
