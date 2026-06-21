#include "Editor/RenderPass/SceneGridRenderPass.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Render/RenderResource.h"
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
        inline constexpr const char* SceneGridVertexShaderName = "SceneGrid.Vertex";
        inline constexpr const char* SceneGridFragmentShaderName = "SceneGrid.Fragment";
        const ShaderID SceneGridVertexShaderID{SceneGridVertexShaderName, 0};
        const ShaderID SceneGridFragmentShaderID{SceneGridFragmentShaderName, 0};

        constexpr Float32 GridExtent = 1000.0f;

        const char* SceneGridShaderSource = R"(
cbuffer SceneGridConstants : register(b0)
{
    float4x4 worldViewProjection;
    float4 gridParams;
    float4 minorColor;
    float4 majorColor;
    float4 xAxisColor;
    float4 zAxisColor;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(worldViewProjection, float4(input.position, 1.0f));
    output.worldPosition = input.position;
    return output;
}

float GridLine(float coordinate, float spacing, float width)
{
    float scaledCoordinate = coordinate / spacing;
    float distanceToLine = abs(frac(scaledCoordinate - 0.5f) - 0.5f);
    float antiAlias = max(fwidth(scaledCoordinate), 0.0001f);
    return 1.0f - smoothstep(0.0f, antiAlias * width, distanceToLine);
}

float AxisLine(float coordinate, float width)
{
    float antiAlias = max(fwidth(coordinate), 0.0001f);
    return 1.0f - smoothstep(0.0f, antiAlias * width, abs(coordinate));
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float unitSize = max(gridParams.x, 0.001f);
    float opacity = saturate(gridParams.y);
    float lineWidth = max(gridParams.z, 0.5f);
    float majorEvery = max(gridParams.w, 1.0f);
    float distanceFade = 1.0f - saturate(length(input.worldPosition.xz) / 600.0f);

    float minorLine = max(GridLine(input.worldPosition.x, unitSize, lineWidth), GridLine(input.worldPosition.z, unitSize, lineWidth));
    float majorSpacing = unitSize * majorEvery;
    float majorLine = max(GridLine(input.worldPosition.x, majorSpacing, lineWidth * 1.35f), GridLine(input.worldPosition.z, majorSpacing, lineWidth * 1.35f));
    float xAxis = AxisLine(input.worldPosition.z, lineWidth * 1.8f);
    float zAxis = AxisLine(input.worldPosition.x, lineWidth * 1.8f);

    float4 color = minorColor;
    color = lerp(color, majorColor, saturate(majorLine));
    color = lerp(color, xAxisColor, saturate(xAxis));
    color = lerp(color, zAxisColor, saturate(zAxis));
    color.a *= saturate(max(max(minorLine, majorLine), max(xAxis, zAxis)) * opacity * (0.25f + distanceFade * 0.75f));
    return color;
}
)";

        struct SceneGridUniformData
        {
            Float32 worldViewProjection[16] = {};
            Float32 gridParams[4] = {1.0f, 0.45f, 0.025f, 10.0f};
            Float32 minorColor[4] = {0.45f, 0.48f, 0.52f, 1.0f};
            Float32 majorColor[4] = {0.72f, 0.75f, 0.80f, 1.0f};
            Float32 xAxisColor[4] = {0.95f, 0.20f, 0.20f, 1.0f};
            Float32 zAxisColor[4] = {0.22f, 0.42f, 0.95f, 1.0f};
            Float32 padding[28] = {};
        };

        static_assert(sizeof(SceneGridUniformData) == 256);

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

        [[nodiscard]] Matrix44 BuildViewProjectionMatrix(const std::shared_ptr<RTCamera>& camera) noexcept
        {
            if (camera == nullptr)
            {
                return Matrix44::Identity();
            }

            const RTCameraDesc& cameraDesc = camera->GetDesc();
            return BuildProjectionMatrix(cameraDesc) * BuildRigidInverse(cameraDesc.localToWorld);
        }

        [[nodiscard]] SceneGridUniformData BuildUniformData(const std::shared_ptr<RTCamera>& camera, const SceneGridRenderPassDesc& desc) noexcept
        {
            SceneGridUniformData data = {};
            const Matrix44 worldViewProjection = BuildViewProjectionMatrix(camera).Transposed();
            const std::array<Float32, 16>& values = worldViewProjection.GetValues();
            std::memcpy(data.worldViewProjection, values.data(), sizeof(data.worldViewProjection));
            data.gridParams[0] = std::max(desc.unitSize, 0.001f);
            data.gridParams[1] = Clamp(desc.opacity, 0.0f, 1.0f);
            data.gridParams[2] = 1.25f;
            data.gridParams[3] = 10.0f;
            return data;
        }

        [[nodiscard]] std::vector<RTMeshVertex> BuildGridPlaneVertices()
        {
            RTMeshVertex a = {};
            a.position[0] = -GridExtent;
            a.position[1] = 0.0f;
            a.position[2] = -GridExtent;

            RTMeshVertex b = {};
            b.position[0] = GridExtent;
            b.position[1] = 0.0f;
            b.position[2] = -GridExtent;

            RTMeshVertex c = {};
            c.position[0] = GridExtent;
            c.position[1] = 0.0f;
            c.position[2] = GridExtent;

            RTMeshVertex d = {};
            d.position[0] = -GridExtent;
            d.position[1] = 0.0f;
            d.position[2] = GridExtent;

            // Draw both windings so the grid remains visible regardless of backend front-face convention.
            return {a, b, c, a, c, d, a, c, b, a, d, c};
        }
    } // namespace

    SceneGridRenderPass::SceneGridRenderPass(SceneGridRenderPassDesc desc)
        : desc_(std::move(desc))
    {
    }

    const char* SceneGridRenderPass::GetName() const noexcept
    {
        return "SceneGridPass";
    }

    void SceneGridRenderPass::Setup(RenderPassBuilder& builder)
    {
        if (desc_.colorTexture == nullptr || desc_.colorTexture->GetTexture() == nullptr)
        {
            return;
        }

        builder.AddTextureColorAttachment(*desc_.colorTexture->GetTexture(), rhi::RhiLoadAction::Load, rhi::RhiStoreAction::Store, rhi::RhiColor{});
        if (desc_.colorTexture->GetDepthTexture() != nullptr)
        {
            builder.SetDepthStencilAttachment(
                *desc_.colorTexture->GetDepthTexture(), rhi::RhiLoadAction::Load, rhi::RhiStoreAction::DontCare, rhi::RhiDepthStencilClearValue{});
        }
    }

    void SceneGridRenderPass::Execute(RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        if (desc_.opacity <= 0.0f)
        {
            return;
        }

        EnsureResources(context);
        EnsurePipeline(context);
        UploadUniforms(context);

        rhi::RhiCommandList& commandList = context.GetCommandList();
        commandList.SetPipeline(*pipelineState_);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 0, *uniformBuffer_, 0);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *uniformBuffer_, 0);
        commandList.SetVertexBuffer(0, *vertexBuffer_, sizeof(RTMeshVertex), 0);
        commandList.Draw(12, 0);
    }

    void SceneGridRenderPass::EnsureResources(RenderPassContext& context)
    {
        if (vertexBuffer_ != nullptr)
        {
            return;
        }

        const std::vector<RTMeshVertex> vertices = BuildGridPlaneVertices();
        vertexBuffer_ = context.GetDevice().CreateBuffer(
            MakeBufferDesc(static_cast<UInt64>(vertices.size() * sizeof(RTMeshVertex)), rhi::RhiBufferUsage::Vertex, vertices.data(), "SceneGridVertexBuffer"));
        VE_ASSERT_MESSAGE(vertexBuffer_ != nullptr, "SceneGridRenderPass failed to create vertex buffer.");
    }

    void SceneGridRenderPass::EnsurePipeline(RenderPassContext& context)
    {
        const bool depthEnabled = context.GetRenderPassDesc().hasDepthStencilAttachment;
        const rhi::RhiFormat targetFormat = desc_.colorTexture != nullptr ? desc_.colorTexture->GetDesc().colorFormat : context.GetFrameData().mainSwapchain->GetColorFormat();
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineDepthEnabled_ == depthEnabled)
        {
            return;
        }

        ShaderManager* shaderManager = context.GetFrameData().shaderManager;
        VE_ASSERT_MESSAGE(shaderManager != nullptr, "SceneGridRenderPass requires a ShaderManager.");

        rhi::RhiShaderModuleDesc vertexShaderDesc = {};
        vertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
        vertexShaderDesc.source = SceneGridShaderSource;
        vertexShaderDesc.entryPoint = "VSMain";
        vertexShaderDesc.debugName = "SceneGridVertexShader";

        rhi::RhiShaderModule* vertexShader = shaderManager->GetOrCompileShader(context.GetDevice(), SceneGridVertexShaderID, vertexShaderDesc);
        VE_ASSERT_MESSAGE(vertexShader != nullptr, "SceneGridRenderPass failed to get vertex shader.");

        rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
        fragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
        fragmentShaderDesc.source = SceneGridShaderSource;
        fragmentShaderDesc.entryPoint = "PSMain";
        fragmentShaderDesc.debugName = "SceneGridFragmentShader";

        rhi::RhiShaderModule* fragmentShader = shaderManager->GetOrCompileShader(context.GetDevice(), SceneGridFragmentShaderID, fragmentShaderDesc);
        VE_ASSERT_MESSAGE(fragmentShader != nullptr, "SceneGridRenderPass failed to get fragment shader.");

        rhi::RhiVertexAttributeDesc positionAttribute = {};
        positionAttribute.semanticName = "POSITION";
        positionAttribute.semanticIndex = 0;
        positionAttribute.format = rhi::RhiFormat::Rgb32Float;
        positionAttribute.offset = 0;

        rhi::RhiVertexAttributeDesc normalAttribute = {};
        normalAttribute.semanticName = "NORMAL";
        normalAttribute.semanticIndex = 0;
        normalAttribute.format = rhi::RhiFormat::Rgb32Float;
        normalAttribute.offset = sizeof(Float32) * 3;

        const rhi::RhiVertexAttributeDesc vertexAttributes[] = {positionAttribute, normalAttribute};

        rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.vertexShader = vertexShader;
        pipelineDesc.fragmentShader = fragmentShader;
        pipelineDesc.vertexLayout.attributes = vertexAttributes;
        pipelineDesc.vertexLayout.attributeCount = 2;
        pipelineDesc.vertexLayout.stride = sizeof(RTMeshVertex);
        pipelineDesc.topology = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.fillMode = rhi::RhiFillMode::Solid;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.depthTestEnabled = depthEnabled;
        pipelineDesc.depthWriteEnabled = false;
        pipelineDesc.alphaBlendEnabled = true;
        pipelineDesc.debugName = "SceneGridPipeline";

        pipelineState_ = context.GetDevice().CreateGraphicsPipeline(pipelineDesc);
        VE_ASSERT_MESSAGE(pipelineState_ != nullptr, "SceneGridRenderPass failed to create pipeline state.");
        pipelineColorFormat_ = targetFormat;
        pipelineDepthEnabled_ = depthEnabled;
    }

    void SceneGridRenderPass::UploadUniforms(RenderPassContext& context)
    {
        const SceneGridUniformData uniformData = BuildUniformData(context.GetRendererData().camera, desc_);
        uniformBuffer_ = context.GetDevice().CreateBuffer(
            MakeBufferDesc(sizeof(SceneGridUniformData), rhi::RhiBufferUsage::Uniform, &uniformData, "SceneGridUniformBuffer"));
        VE_ASSERT_MESSAGE(uniformBuffer_ != nullptr, "SceneGridRenderPass failed to create uniform buffer.");
    }
} // namespace ve
