#include "Editor/RenderPass/EditorGizmoRenderPass.h"

#include "Editor/Core/EditorBuiltinResources.h"
#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <array>
#include <cstring>
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
cbuffer EditorGizmoConstants : register(b0)
{
    float4x4 worldViewProjection;
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
    output.position = mul(worldViewProjection, float4(input.position, 1.0f));
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

struct EditorGizmoConstants
{
    float4x4 worldViewProjection;
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

[[vertex]] VSOutput VSMain(VSInput input [[stage_in]], constant EditorGizmoConstants* constants [[buffer(0)]])
{
    VSOutput output;
    output.position = constants->worldViewProjection * float4(input.position, 1.0f);
    output.color = input.color;
    return output;
}

[[fragment]] float4 PSMain(VSOutput input [[stage_in]])
{
    return float4(clamp(input.color, 0.0f, 1.0f), 1.0f);
}
)";

        const char* EditorGizmoIconShaderSource = R"(
cbuffer EditorGizmoConstants : register(b0)
{
    float4x4 worldViewProjection;
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
    output.position = mul(worldViewProjection, float4(input.position, 1.0f));
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

struct EditorGizmoConstants
{
    float4x4 worldViewProjection;
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

[[vertex]] VSOutput VSMain(VSInput input [[stage_in]], constant EditorGizmoConstants* constants [[buffer(0)]])
{
    VSOutput output;
    output.position = constants->worldViewProjection * float4(input.position, 1.0f);
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

        struct EditorGizmoUniformData
        {
            Float32 worldViewProjection[16] = {};
            Float32 padding[48] = {};
        };

        static_assert(sizeof(EditorGizmoUniformData) == 256);

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

        [[nodiscard]] Matrix44 BuildRigidInverse(const Matrix44& localToWorld) noexcept
        {
            Matrix44 inverse = Matrix44::Identity();
            for (SizeT row = 0; row < 3; ++row)
            {
                for (SizeT column = 0; column < 3; ++column)
                {
                    inverse.Set(row, column, localToWorld.Get(column, row));
                }
            }

            const Vector3 translation(localToWorld.Get(0, 3), localToWorld.Get(1, 3), localToWorld.Get(2, 3));
            for (SizeT row = 0; row < 3; ++row)
            {
                const Float32 value =
                    -((inverse.Get(row, 0) * translation.GetX()) + (inverse.Get(row, 1) * translation.GetY()) + (inverse.Get(row, 2) * translation.GetZ()));
                inverse.Set(row, 3, value);
            }

            return inverse;
        }

        [[nodiscard]] Matrix44 BuildPerspectiveProjection(const RTCamera& camera) noexcept
        {
            const Float32 nearClip = std::max(camera.GetNearClipPlane(), 0.001f);
            const Float32 farClip = std::max(camera.GetFarClipPlane(), nearClip + 0.001f);
            const Float32 aspectRatio = std::max(camera.GetAspectRatio(), 0.001f);
            const Float32 fieldOfView = std::max(camera.GetVerticalFieldOfViewRadians(), 0.001f);
            const Float32 yScale = 1.0f / std::tan(fieldOfView * 0.5f);
            const Float32 xScale = yScale / aspectRatio;

            Matrix44 projection = Matrix44::Zero();
            projection.Set(0, 0, xScale);
            projection.Set(1, 1, yScale);
            projection.Set(2, 2, farClip / (farClip - nearClip));
            projection.Set(2, 3, -(nearClip * farClip) / (farClip - nearClip));
            projection.Set(3, 2, 1.0f);
            return projection;
        }

        [[nodiscard]] Matrix44 BuildOrthographicProjection(const RTCamera& camera) noexcept
        {
            const Float32 nearClip = camera.GetNearClipPlane();
            const Float32 farClip = std::max(camera.GetFarClipPlane(), nearClip + 0.001f);
            const Float32 aspectRatio = std::max(camera.GetAspectRatio(), 0.001f);
            const Float32 height = std::max(camera.GetOrthographicSize(), 0.001f);
            const Float32 width = height * aspectRatio;

            Matrix44 projection = Matrix44::Identity();
            projection.Set(0, 0, 2.0f / width);
            projection.Set(1, 1, 2.0f / height);
            projection.Set(2, 2, 1.0f / (farClip - nearClip));
            projection.Set(2, 3, -nearClip / (farClip - nearClip));
            return projection;
        }

        [[nodiscard]] Matrix44 BuildProjectionMatrix(const RTCamera& camera) noexcept
        {
            return camera.GetProjectionMode() == RTCameraProjectionMode::Orthographic ? BuildOrthographicProjection(camera)
                                                                                     : BuildPerspectiveProjection(camera);
        }

        [[nodiscard]] EditorGizmoUniformData BuildUniformData(const std::shared_ptr<RTCamera>& camera) noexcept
        {
            EditorGizmoUniformData data = {};
            const Matrix44 viewProjection =
                camera != nullptr ? BuildProjectionMatrix(*camera) * BuildRigidInverse(camera->GetLocalToWorld()) : Matrix44::Identity();
            const Matrix44 shaderWorldViewProjection = viewProjection.Transposed();
            const std::array<Float32, 16>& values = shaderWorldViewProjection.GetValues();
            std::memcpy(data.worldViewProjection, values.data(), sizeof(data.worldViewProjection));
            return data;
        }
    } // namespace

    EditorGizmoRenderPass::EditorGizmoRenderPass(EditorGizmoRenderPassInitParam initParam)
        : initParam_(std::move(initParam))
    {
    }

    const char* EditorGizmoRenderPass::GetName() const noexcept
    {
        return "EditorGizmoPass";
    }

    void EditorGizmoRenderPass::Setup(RenderPassBuilder& builder)
    {
        if (initParam_.colorTexture == nullptr || initParam_.colorTexture->GetTexture() == nullptr)
        {
            return;
        }

        builder.AddTextureColorAttachment(*initParam_.colorTexture->GetTexture(), rhi::RhiLoadAction::Load, rhi::RhiStoreAction::Store, rhi::RhiColor{});
        if (initParam_.colorTexture->GetDepthTexture() != nullptr)
        {
            builder.SetDepthStencilAttachment(
                *initParam_.colorTexture->GetDepthTexture(), rhi::RhiLoadAction::Load, rhi::RhiStoreAction::DontCare, rhi::RhiDepthStencilClearValue{});
        }
    }

    void EditorGizmoRenderPass::Execute(RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        if (initParam_.drawList == nullptr || (initParam_.drawList->lines.empty() && initParam_.drawList->icons.empty()))
        {
            return;
        }

        EnsurePipeline(context);
        if (linePipelineState_ == nullptr || iconPipelineState_ == nullptr)
        {
            return;
        }
        EnsureIconResources(context);
        UploadFrameResources(context);

        rhi::RhiCommandList& commandList = context.commandList;
        if (uploadedIconVertexCount_ > 0)
        {
            commandList.SetPipeline(*iconPipelineState_);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 0, *uniformBuffer_, 0);
            commandList.SetTexture(rhi::RhiShaderStage::Fragment, 0, *iconAtlasTexture_);
            commandList.SetSampler(rhi::RhiShaderStage::Fragment, 0, *iconSampler_);
            commandList.SetVertexBuffer(0, *iconVertexBuffer_, sizeof(EditorGizmoIconVertex), 0);
            commandList.Draw(static_cast<UInt32>(uploadedIconVertexCount_), 0);
        }

        if (uploadedLineVertexCount_ > 0)
        {
            commandList.SetPipeline(*linePipelineState_);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 0, *uniformBuffer_, 0);
            commandList.SetVertexBuffer(0, *lineVertexBuffer_, sizeof(EditorGizmoVertex), 0);
            commandList.Draw(static_cast<UInt32>(uploadedLineVertexCount_), 0);
        }
    }

    void EditorGizmoRenderPass::EnsurePipeline(RenderPassContext& context)
    {
        const rhi::RhiFormat targetFormat = ResolveTargetFormat(context);
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
        linePipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::LineList;
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

        rhi::RhiGraphicsPipelineDesc iconPipelineDesc = {};
        iconPipelineDesc.blendState = rhi::StaticRenderStates::AlphaBlend;
        iconPipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidNoCullRasterizer;
        iconPipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
        iconPipelineDesc.boundShaderState.vertexShader = iconVertexShader;
        iconPipelineDesc.boundShaderState.fragmentShader = iconFragmentShader;
        iconPipelineDesc.boundShaderState.vertexDeclaration.attributes = iconVertexAttributes;
        iconPipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 3;
        iconPipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(EditorGizmoIconVertex);
        iconPipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
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

    void EditorGizmoRenderPass::EnsureIconResources(RenderPassContext& context)
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
            VE_ASSERT_MESSAGE(iconAtlasTexture_ != nullptr, "EditorGizmoRenderPass failed to create builtin gizmo icon atlas texture.");
        }

        if (iconSampler_ == nullptr)
        {
            iconSampler_ = context.device.CreateSampler(rhi::StaticRenderStates::BilinearClampSampler);
            VE_ASSERT_MESSAGE(iconSampler_ != nullptr, "EditorGizmoRenderPass failed to create builtin gizmo icon sampler.");
        }
    }

    void EditorGizmoRenderPass::UploadFrameResources(RenderPassContext& context)
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
            VE_ASSERT_MESSAGE(lineVertexBuffer_ != nullptr, "EditorGizmoRenderPass failed to create line vertex buffer.");
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
            VE_ASSERT_MESSAGE(iconVertexBuffer_ != nullptr, "EditorGizmoRenderPass failed to create icon vertex buffer.");
        }
        else
        {
            iconVertexBuffer_.reset();
        }

        const EditorGizmoUniformData uniformData = BuildUniformData(context.rendererData.resolvedCamera);
        uniformBuffer_ =
            context.device.CreateBuffer(MakeBufferDesc(sizeof(EditorGizmoUniformData), rhi::RhiBufferUsage::Uniform, &uniformData, "EditorGizmoUniformBuffer"));
        VE_ASSERT_MESSAGE(uniformBuffer_ != nullptr, "EditorGizmoRenderPass failed to create uniform buffer.");
    }

    rhi::RhiFormat EditorGizmoRenderPass::ResolveTargetFormat(const RenderPassContext& context) const noexcept
    {
        if (initParam_.colorTexture != nullptr)
        {
            return initParam_.colorTexture->GetDesc().colorFormat;
        }

        VE_ASSERT(context.frameData.mainSwapchain != nullptr);
        return context.frameData.mainSwapchain->GetColorFormat();
    }
} // namespace ve
