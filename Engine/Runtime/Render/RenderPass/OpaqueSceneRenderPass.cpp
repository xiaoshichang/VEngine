#include "Engine/Runtime/Render/RenderPass/OpaqueSceneRenderPass.h"

#include "Engine/Runtime/Logging/Log.h"
#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

namespace ve
{
    namespace
    {
        struct OpaqueSceneObjectUniformData
        {
            Float32 worldViewProjection[16] = {};
            Float32 localToWorld[16] = {};
            Float32 padding[32] = {};
        };

        struct OpaqueSceneLightUniformData
        {
            Float32 lightDirectionAndIntensity[4] = {0.0f, 0.0f, 1.0f, 0.0f};
            Float32 lightColorAndAmbient[4] = {1.0f, 1.0f, 1.0f, 0.35f};
            Float32 padding[56] = {};
        };

        static_assert(sizeof(OpaqueSceneObjectUniformData) == 256);
        static_assert(sizeof(OpaqueSceneLightUniformData) == 256);

        [[nodiscard]] rhi::RhiBufferDesc MakeUniformBufferDesc(UInt64 size, const void* initialData, const char* debugName) noexcept
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = rhi::RhiBufferUsage::Uniform;
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

        [[nodiscard]] OpaqueSceneObjectUniformData BuildObjectUniformData(const Matrix44& worldViewProjection, const Matrix44& localToWorld) noexcept
        {
            OpaqueSceneObjectUniformData data = {};
            const Matrix44 shaderWorldViewProjection = worldViewProjection.Transposed();
            const Matrix44 shaderLocalToWorld = localToWorld.Transposed();
            const std::array<Float32, 16>& worldViewProjectionValues = shaderWorldViewProjection.GetValues();
            const std::array<Float32, 16>& localToWorldValues = shaderLocalToWorld.GetValues();
            std::memcpy(data.worldViewProjection, worldViewProjectionValues.data(), sizeof(data.worldViewProjection));
            std::memcpy(data.localToWorld, localToWorldValues.data(), sizeof(data.localToWorld));
            return data;
        }

        [[nodiscard]] std::shared_ptr<RTLight> FindDirectionalLight(const RTScene& scene) noexcept
        {
            for (SizeT lightIndex = 0; lightIndex < scene.GetLightCount(); ++lightIndex)
            {
                std::shared_ptr<RTLight> light = scene.GetLight(lightIndex);
                if (light != nullptr && light->GetDesc().type == RTLightType::Directional)
                {
                    return light;
                }
            }

            return nullptr;
        }

        [[nodiscard]] OpaqueSceneLightUniformData BuildLightUniformData(const RTScene& scene) noexcept
        {
            OpaqueSceneLightUniformData data = {};
            const std::shared_ptr<RTLight> light = FindDirectionalLight(scene);
            if (light == nullptr)
            {
                return data;
            }

            const RTLightDesc& lightDesc = light->GetDesc();
            const Vector3 direction = lightDesc.direction.Normalized();
            data.lightDirectionAndIntensity[0] = direction.GetX();
            data.lightDirectionAndIntensity[1] = direction.GetY();
            data.lightDirectionAndIntensity[2] = direction.GetZ();
            data.lightDirectionAndIntensity[3] = std::max(lightDesc.intensity, 0.0f);
            data.lightColorAndAmbient[0] = std::max(lightDesc.color.GetX(), 0.0f);
            data.lightColorAndAmbient[1] = std::max(lightDesc.color.GetY(), 0.0f);
            data.lightColorAndAmbient[2] = std::max(lightDesc.color.GetZ(), 0.0f);
            data.lightColorAndAmbient[3] = 0.35f;
            return data;
        }

        [[nodiscard]] std::shared_ptr<RTShaderResource> FindFirstShaderResource(const RTScene& scene) noexcept
        {
            for (SizeT itemIndex = 0; itemIndex < scene.GetRenderItemCount(); ++itemIndex)
            {
                const std::shared_ptr<RTRenderItem> item = scene.GetRenderItem(itemIndex);
                if (item == nullptr)
                {
                    continue;
                }

                const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(item->GetDesc().materialResource);
                if (materialResource == nullptr)
                {
                    continue;
                }

                std::shared_ptr<RTShaderResource> shaderResource = materialResource->GetShaderResource();
                if (shaderResource != nullptr)
                {
                    return shaderResource;
                }
            }

            return nullptr;
        }

        [[nodiscard]] rhi::RhiColor ResolvePassClearColor(const RendererData& rendererData) noexcept
        {
            VE_ASSERT_ALWAYS_MESSAGE(rendererData.resolvedCamera != nullptr, "OpaqueScenePass requires a resolved camera for clear color.");
            if (rendererData.resolvedCamera == nullptr)
            {
                return rhi::RhiColor{};
            }

            return rendererData.resolvedCamera->GetDesc().clearColor;
        }
    } // namespace

    OpaqueSceneRenderPass::OpaqueSceneRenderPass(OpaqueSceneRenderPassInitParam initParam)
        : target_(std::move(initParam.target))
        , fillMode_(initParam.fillMode)
    {
    }

    const char* OpaqueSceneRenderPass::GetName() const noexcept
    {
        return "OpaqueScenePass";
    }

    void OpaqueSceneRenderPass::Setup(RenderPassBuilder& builder)
    {
        const rhi::RhiColor clearColor = ResolvePassClearColor(builder.rendererData);
        if (target_.colorTexture != nullptr && target_.colorTexture->GetTexture() != nullptr)
        {
            builder.AddTextureColorAttachment(*target_.colorTexture->GetTexture(), rhi::RhiLoadAction::Clear, target_.colorStoreAction, clearColor);
            if (target_.colorTexture->GetDepthTexture() != nullptr)
            {
                builder.SetDepthStencilAttachment(
                    *target_.colorTexture->GetDepthTexture(), rhi::RhiLoadAction::Clear, rhi::RhiStoreAction::DontCare, rhi::RhiDepthStencilClearValue{});
            }
            return;
        }

        builder.AddSwapchainColorAttachment(rhi::RhiLoadAction::Clear, target_.colorStoreAction, clearColor);
    }

    void OpaqueSceneRenderPass::Execute(RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();

        const std::shared_ptr<RTScene> scene = context.rendererData.scene;
        if (scene == nullptr)
        {
            return;
        }

        EnsurePipeline(context);
        if (pipelineState_ == nullptr)
        {
            return;
        }

        rhi::RhiCommandList& commandList = context.commandList;
        commandList.SetPipeline(*pipelineState_);

        frameUniformBuffers_.clear();
        BindLightUniform(context, *scene);
        const Matrix44 viewProjection = BuildViewProjectionMatrix(context.rendererData.resolvedCamera);
        for (SizeT itemIndex = 0; itemIndex < scene->GetRenderItemCount(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem> item = scene->GetRenderItem(itemIndex);
            if (item == nullptr)
            {
                VE_LOG_WARN("OpaqueScenePass skipping null render item at index %zu.", itemIndex);
                continue;
            }

            const auto meshResource = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
            if (meshResource == nullptr || meshResource->GetVertexBuffer() == nullptr)
            {
                VE_LOG_WARN("OpaqueScenePass skipping render item at index %zu due to missing or invalid mesh resource.", itemIndex);
                continue;
            }

            const RTRenderItemDesc& itemDesc = item->GetDesc();
            const Matrix44 worldViewProjection = viewProjection * itemDesc.localToWorld;
            const OpaqueSceneObjectUniformData objectUniformData = BuildObjectUniformData(worldViewProjection, itemDesc.localToWorld);
            std::unique_ptr<rhi::RhiBuffer> objectUniformBuffer = context.device.CreateBuffer(
                MakeUniformBufferDesc(sizeof(OpaqueSceneObjectUniformData), &objectUniformData, "OpaqueSceneObjectUniformBuffer"));
            VE_ASSERT_MESSAGE(objectUniformBuffer != nullptr, "OpaqueScenePass failed to create object uniform buffer.");
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 0, *objectUniformBuffer, 0);
            frameUniformBuffers_.push_back(std::move(objectUniformBuffer));

            if (!BindMaterialUniform(context, itemDesc))
            {
                continue;
            }

            commandList.SetVertexBuffer(0, *meshResource->GetVertexBuffer(), meshResource->GetVertexStride(), 0);
            if (meshResource->GetIndexBuffer() != nullptr && meshResource->GetIndexCount() > 0)
            {
                commandList.SetIndexBuffer(*meshResource->GetIndexBuffer(), rhi::RhiIndexFormat::UInt32, 0);
                commandList.DrawIndexed(meshResource->GetIndexCount(), 0, 0);
            }
            else
            {
                commandList.Draw(meshResource->GetVertexCount(), 0);
            }
        }
    }

    void OpaqueSceneRenderPass::EnsurePipeline(RenderPassContext& context)
    {
        const std::shared_ptr<RTScene> scene = context.rendererData.scene;
        VE_ASSERT_ALWAYS_MESSAGE(scene != nullptr, "OpaqueScenePass requires a render scene.");
        if (scene == nullptr)
        {
            return;
        }

        std::shared_ptr<RTShaderResource> shaderResource = FindFirstShaderResource(*scene);
        VE_ASSERT_ALWAYS_MESSAGE(shaderResource != nullptr, "OpaqueScenePass requires a material shader resource.");
        if (shaderResource == nullptr)
        {
            return;
        }

        rhi::RhiShaderModule* vertexShader = shaderResource->GetVertexShader();
        rhi::RhiShaderModule* fragmentShader = shaderResource->GetFragmentShader();
        VE_ASSERT_ALWAYS_MESSAGE(vertexShader != nullptr && fragmentShader != nullptr, "OpaqueScenePass material shader resource is not initialized.");
        if (vertexShader == nullptr || fragmentShader == nullptr)
        {
            return;
        }

        const rhi::RhiFormat targetFormat = ResolveTargetFormat(context);
        const bool depthEnabled = context.passData.renderPassDesc.hasDepthStencilAttachment;
        const bool sameResourceShader = pipelineShaderResource_.lock() == shaderResource;
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineFillMode_ == fillMode_ && pipelineDepthEnabled_ == depthEnabled &&
            sameResourceShader)
        {
            return;
        }

        rhi::RhiDevice& device = context.device;

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
        pipelineDesc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
        pipelineDesc.rasterizerState.fillMode = fillMode_;
        pipelineDesc.depthStencilState = depthEnabled ? rhi::StaticRenderStates::DepthReadWriteLessEqual : rhi::StaticRenderStates::DepthDisabled;
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.boundShaderState.fragmentShader = fragmentShader;
        pipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        pipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 2;
        pipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(RTMeshVertex);
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.debugName = "OpaqueScenePipeline";

        pipelineState_ = device.CreateGraphicsPipeline(pipelineDesc);
        VE_ASSERT_MESSAGE(pipelineState_ != nullptr, "OpaqueScenePass failed to create pipeline state.");
        pipelineColorFormat_ = targetFormat;
        pipelineFillMode_ = fillMode_;
        pipelineDepthEnabled_ = depthEnabled;
        pipelineShaderResource_ = shaderResource;
    }

    void OpaqueSceneRenderPass::BindLightUniform(RenderPassContext& context, const RTScene& scene)
    {
        const OpaqueSceneLightUniformData lightUniformData = BuildLightUniformData(scene);
        std::unique_ptr<rhi::RhiBuffer> lightUniformBuffer =
            context.device.CreateBuffer(MakeUniformBufferDesc(sizeof(OpaqueSceneLightUniformData), &lightUniformData, "OpaqueSceneLightUniformBuffer"));
        VE_ASSERT_MESSAGE(lightUniformBuffer != nullptr, "OpaqueScenePass failed to create light uniform buffer.");
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 2, *lightUniformBuffer, 0);
        frameUniformBuffers_.push_back(std::move(lightUniformBuffer));
    }

    bool OpaqueSceneRenderPass::BindMaterialUniform(RenderPassContext& context, const RTRenderItemDesc& itemDesc)
    {
        const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(itemDesc.materialResource);
        if (materialResource != nullptr && materialResource->GetUniformBuffer() != nullptr)
        {
            context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 1, *materialResource->GetUniformBuffer(), 0);
            return true;
        }

        VE_ASSERT_ALWAYS_MESSAGE(false, "OpaqueScenePass requires a material resource with an initialized uniform buffer.");
        return false;
    }

    rhi::RhiFormat OpaqueSceneRenderPass::ResolveTargetFormat(const RenderPassContext& context) const noexcept
    {
        if (target_.colorTexture != nullptr)
        {
            return target_.colorTexture->GetDesc().colorFormat;
        }

        VE_ASSERT(context.frameData.mainSwapchain != nullptr);
        return context.frameData.mainSwapchain->GetColorFormat();
    }
} // namespace ve
