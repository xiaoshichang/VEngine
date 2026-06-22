#include "Editor/RenderPass/EditorGizmoRenderPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace ve
{
    namespace
    {
        inline constexpr const char* EditorGizmoVertexShaderName = "EditorGizmo.Vertex";
        inline constexpr const char* EditorGizmoFragmentShaderName = "EditorGizmo.Fragment";
        const ShaderID EditorGizmoVertexShaderID{EditorGizmoVertexShaderName, 0};
        const ShaderID EditorGizmoFragmentShaderID{EditorGizmoFragmentShaderName, 0};

        const char* EditorGizmoShaderSource = R"(
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

        [[nodiscard]] Matrix44 BuildPerspectiveProjection(const RTCameraDesc& cameraDesc) noexcept
        {
            const Float32 nearClip = std::max(cameraDesc.nearClipPlane, 0.001f);
            const Float32 farClip = std::max(cameraDesc.farClipPlane, nearClip + 0.001f);
            const Float32 aspectRatio = std::max(cameraDesc.aspectRatio, 0.001f);
            const Float32 fieldOfView = std::max(cameraDesc.verticalFieldOfViewRadians, 0.001f);
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

        [[nodiscard]] Matrix44 BuildOrthographicProjection(const RTCameraDesc& cameraDesc) noexcept
        {
            const Float32 nearClip = cameraDesc.nearClipPlane;
            const Float32 farClip = std::max(cameraDesc.farClipPlane, nearClip + 0.001f);
            const Float32 aspectRatio = std::max(cameraDesc.aspectRatio, 0.001f);
            const Float32 height = std::max(cameraDesc.orthographicSize, 0.001f);
            const Float32 width = height * aspectRatio;

            Matrix44 projection = Matrix44::Identity();
            projection.Set(0, 0, 2.0f / width);
            projection.Set(1, 1, 2.0f / height);
            projection.Set(2, 2, 1.0f / (farClip - nearClip));
            projection.Set(2, 3, -nearClip / (farClip - nearClip));
            return projection;
        }

        [[nodiscard]] Matrix44 BuildProjectionMatrix(const RTCameraDesc& cameraDesc) noexcept
        {
            return cameraDesc.projectionMode == RTCameraProjectionMode::Orthographic ? BuildOrthographicProjection(cameraDesc)
                                                                                     : BuildPerspectiveProjection(cameraDesc);
        }

        [[nodiscard]] EditorGizmoUniformData BuildUniformData(const std::shared_ptr<RTCamera>& camera) noexcept
        {
            EditorGizmoUniformData data = {};
            const Matrix44 viewProjection =
                camera != nullptr ? BuildProjectionMatrix(camera->GetDesc()) * BuildRigidInverse(camera->GetDesc().localToWorld) : Matrix44::Identity();
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
        if (initParam_.drawList == nullptr || initParam_.drawList->vertices.empty())
        {
            return;
        }

        EnsurePipeline(context);
        UploadFrameResources(context);

        rhi::RhiCommandList& commandList = context.commandList;
        commandList.SetPipeline(*pipelineState_);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 0, *uniformBuffer_, 0);
        commandList.SetVertexBuffer(0, *vertexBuffer_, sizeof(EditorGizmoVertex), 0);
        commandList.Draw(static_cast<UInt32>(uploadedVertexCount_), 0);
    }

    void EditorGizmoRenderPass::EnsurePipeline(RenderPassContext& context)
    {
        const rhi::RhiFormat targetFormat = ResolveTargetFormat(context);
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat)
        {
            return;
        }

        ShaderManager* shaderManager = context.frameData.shaderManager;
        VE_ASSERT_MESSAGE(shaderManager != nullptr, "EditorGizmoRenderPass requires a ShaderManager.");

        rhi::RhiShaderModuleDesc vertexShaderDesc = {};
        vertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
        vertexShaderDesc.source = EditorGizmoShaderSource;
        vertexShaderDesc.entryPoint = "VSMain";
        vertexShaderDesc.debugName = "EditorGizmoVertexShader";

        rhi::RhiShaderModule* vertexShader = shaderManager->GetOrCompileShader(context.device, EditorGizmoVertexShaderID, vertexShaderDesc);
        VE_ASSERT_MESSAGE(vertexShader != nullptr, "EditorGizmoRenderPass failed to get vertex shader.");

        rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
        fragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
        fragmentShaderDesc.source = EditorGizmoShaderSource;
        fragmentShaderDesc.entryPoint = "PSMain";
        fragmentShaderDesc.debugName = "EditorGizmoFragmentShader";

        rhi::RhiShaderModule* fragmentShader = shaderManager->GetOrCompileShader(context.device, EditorGizmoFragmentShaderID, fragmentShaderDesc);
        VE_ASSERT_MESSAGE(fragmentShader != nullptr, "EditorGizmoRenderPass failed to get fragment shader.");

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

        rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.blendState = rhi::StaticRenderStates::AlphaBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
        pipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.boundShaderState.fragmentShader = fragmentShader;
        pipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        pipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 2;
        pipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(EditorGizmoVertex);
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.debugName = "EditorGizmoPipeline";

        pipelineState_ = context.device.CreateGraphicsPipeline(pipelineDesc);
        VE_ASSERT_MESSAGE(pipelineState_ != nullptr, "EditorGizmoRenderPass failed to create pipeline state.");
        pipelineColorFormat_ = targetFormat;
    }

    void EditorGizmoRenderPass::UploadFrameResources(RenderPassContext& context)
    {
        VE_ASSERT(initParam_.drawList != nullptr);
        uploadedVertexCount_ = initParam_.drawList->vertices.size();

        vertexBuffer_ = context.device.CreateBuffer(MakeBufferDesc(static_cast<UInt64>(uploadedVertexCount_ * sizeof(EditorGizmoVertex)),
                                                                   rhi::RhiBufferUsage::Vertex,
                                                                   initParam_.drawList->vertices.data(),
                                                                   "EditorGizmoVertexBuffer"));
        VE_ASSERT_MESSAGE(vertexBuffer_ != nullptr, "EditorGizmoRenderPass failed to create vertex buffer.");

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
