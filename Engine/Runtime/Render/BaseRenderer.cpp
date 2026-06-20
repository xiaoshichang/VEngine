#include "Engine/Runtime/Render/BaseRenderer.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderShaderIDs.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace ve
{
    namespace
    {
        const char* OpaqueSceneShaderSource = R"(
cbuffer ObjectConstants : register(b0)
{
    float4x4 worldViewProjection;
    float4x4 localToWorld;
};

cbuffer MaterialConstants : register(b1)
{
    float4 baseColor;
};

cbuffer LightConstants : register(b2)
{
    float4 lightDirectionAndIntensity;
    float4 lightColorAndAmbient;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(worldViewProjection, float4(input.position, 1.0f));
    output.worldNormal = mul((float3x3)localToWorld, input.normal);
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 normal = normalize(input.worldNormal);
    float3 lightDirection = normalize(lightDirectionAndIntensity.xyz);
    float3 lightToSurface = -lightDirection;
    float diffuse = saturate(dot(normal, lightToSurface));
    float3 lightColor = lightColorAndAmbient.rgb;
    float ambient = lightColorAndAmbient.a;
    float3 litColor = baseColor.rgb * (ambient + (lightColor * lightDirectionAndIntensity.w * diffuse));
    return float4(saturate(litColor), baseColor.a);
}
)";

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
        static_assert(sizeof(RTMaterialUniformData) == 256);

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

        [[nodiscard]] std::shared_ptr<RTCamera> FindFrameCamera(const RTScene& scene) noexcept
        {
            std::shared_ptr<RTCamera> fallback;
            for (SizeT cameraIndex = 0; cameraIndex < scene.GetCameraCount(); ++cameraIndex)
            {
                std::shared_ptr<RTCamera> camera = scene.GetCamera(cameraIndex);
                if (camera == nullptr)
                {
                    continue;
                }

                if (fallback == nullptr)
                {
                    fallback = camera;
                }

                if (camera->GetDesc().primary)
                {
                    return camera;
                }
            }

            return fallback;
        }

        [[nodiscard]] rhi::RhiColor ResolveFrameClearColor(const std::shared_ptr<RTScene>& scene) noexcept
        {
            if (scene == nullptr)
            {
                return rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f};
            }

            const std::shared_ptr<RTCamera> camera = FindFrameCamera(*scene);
            return camera != nullptr ? camera->GetDesc().clearColor : rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f};
        }

        [[nodiscard]] Matrix44 BuildViewProjectionMatrix(const RTScene& scene) noexcept
        {
            const std::shared_ptr<RTCamera> camera = FindFrameCamera(scene);
            if (camera == nullptr)
            {
                return Matrix44::Identity();
            }

            const RTCameraDesc& cameraDesc = camera->GetDesc();
            return BuildProjectionMatrix(cameraDesc) * BuildRigidInverse(cameraDesc.localToWorld);
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

        class OpaqueSceneRenderPass final : public RenderPass
        {
        public:
            OpaqueSceneRenderPass(RendererRenderTarget target, rhi::RhiFillMode fillMode)
                : target_(std::move(target))
                , fillMode_(fillMode)
            {
            }

            [[nodiscard]] const char* GetName() const noexcept override
            {
                return "OpaqueScenePass";
            }

            void Setup(RenderPassBuilder& builder) override
            {
                const rhi::RhiColor clearColor = builder.GetRendererData().clearColor;
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

            void Execute(RenderPassContext& context) override
            {
                VE_ASSERT_RENDER_THREAD();

                EnsurePipeline(context);
                const std::shared_ptr<RTScene> scene = context.GetRendererData().scene;
                if (scene == nullptr)
                {
                    return;
                }

                rhi::RhiCommandList& commandList = context.GetCommandList();
                commandList.SetPipeline(*pipelineState_);

                frameUniformBuffers_.clear();
                BindLightUniform(context, *scene);
                const Matrix44 viewProjection = BuildViewProjectionMatrix(context.GetRendererData().camera);
                for (SizeT itemIndex = 0; itemIndex < scene->GetRenderItemCount(); ++itemIndex)
                {
                    const std::shared_ptr<RTRenderItem> item = scene->GetRenderItem(itemIndex);
                    if (item == nullptr)
                    {
                        continue;
                    }

                    const auto meshResource = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
                    if (meshResource == nullptr || meshResource->GetVertexBuffer() == nullptr)
                    {
                        continue;
                    }

                    const RTRenderItemDesc& itemDesc = item->GetDesc();
                    const Matrix44 worldViewProjection = viewProjection * itemDesc.localToWorld;
                    const OpaqueSceneObjectUniformData objectUniformData = BuildObjectUniformData(worldViewProjection, itemDesc.localToWorld);
                    std::unique_ptr<rhi::RhiBuffer> objectUniformBuffer = context.GetDevice().CreateBuffer(
                        MakeUniformBufferDesc(sizeof(OpaqueSceneObjectUniformData), &objectUniformData, "OpaqueSceneObjectUniformBuffer"));
                    VE_ASSERT_MESSAGE(objectUniformBuffer != nullptr, "OpaqueScenePass failed to create object uniform buffer.");
                    commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 0, *objectUniformBuffer, 0);
                    frameUniformBuffers_.push_back(std::move(objectUniformBuffer));

                    BindMaterialUniform(context, itemDesc);

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

        private:
            void EnsurePipeline(RenderPassContext& context)
            {
                const rhi::RhiFormat targetFormat = ResolveTargetFormat(context);
                const bool depthEnabled = context.GetRenderPassDesc().hasDepthStencilAttachment;
                if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineFillMode_ == fillMode_ &&
                    pipelineDepthEnabled_ == depthEnabled)
                {
                    return;
                }

                rhi::RhiDevice& device = context.GetDevice();

                ShaderManager* shaderManager = context.GetFrameData().shaderManager;
                VE_ASSERT_MESSAGE(shaderManager != nullptr, "OpaqueScenePass requires a ShaderManager.");

                rhi::RhiShaderModuleDesc vertexShaderDesc = {};
                vertexShaderDesc.stage = rhi::RhiShaderStage::Vertex;
                vertexShaderDesc.source = OpaqueSceneShaderSource;
                vertexShaderDesc.entryPoint = "VSMain";
                vertexShaderDesc.debugName = "OpaqueSceneVertexShader";

                rhi::RhiShaderModule* vertexShader = shaderManager->GetOrCompileShader(device, RenderShaderIDs::OpaqueSceneVertex, vertexShaderDesc);
                VE_ASSERT_MESSAGE(vertexShader != nullptr, "OpaqueScenePass failed to get vertex shader.");

                rhi::RhiShaderModuleDesc fragmentShaderDesc = {};
                fragmentShaderDesc.stage = rhi::RhiShaderStage::Fragment;
                fragmentShaderDesc.source = OpaqueSceneShaderSource;
                fragmentShaderDesc.entryPoint = "PSMain";
                fragmentShaderDesc.debugName = "OpaqueSceneFragmentShader";

                rhi::RhiShaderModule* fragmentShader = shaderManager->GetOrCompileShader(device, RenderShaderIDs::OpaqueSceneFragment, fragmentShaderDesc);
                VE_ASSERT_MESSAGE(fragmentShader != nullptr, "OpaqueScenePass failed to get fragment shader.");

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
                pipelineDesc.fillMode = fillMode_;
                pipelineDesc.colorFormat = targetFormat;
                pipelineDesc.depthTestEnabled = depthEnabled;
                pipelineDesc.depthWriteEnabled = depthEnabled;
                pipelineDesc.debugName = "OpaqueScenePipeline";

                pipelineState_ = device.CreateGraphicsPipeline(pipelineDesc);
                VE_ASSERT_MESSAGE(pipelineState_ != nullptr, "OpaqueScenePass failed to create pipeline state.");
                pipelineColorFormat_ = targetFormat;
                pipelineFillMode_ = fillMode_;
                pipelineDepthEnabled_ = depthEnabled;
            }

            void BindLightUniform(RenderPassContext& context, const RTScene& scene)
            {
                const OpaqueSceneLightUniformData lightUniformData = BuildLightUniformData(scene);
                std::unique_ptr<rhi::RhiBuffer> lightUniformBuffer = context.GetDevice().CreateBuffer(
                    MakeUniformBufferDesc(sizeof(OpaqueSceneLightUniformData), &lightUniformData, "OpaqueSceneLightUniformBuffer"));
                VE_ASSERT_MESSAGE(lightUniformBuffer != nullptr, "OpaqueScenePass failed to create light uniform buffer.");
                context.GetCommandList().SetUniformBuffer(rhi::RhiShaderStage::Fragment, 2, *lightUniformBuffer, 0);
                frameUniformBuffers_.push_back(std::move(lightUniformBuffer));
            }

            void BindMaterialUniform(RenderPassContext& context, const RTRenderItemDesc& itemDesc)
            {
                const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(itemDesc.materialResource);
                if (materialResource != nullptr && materialResource->GetUniformBuffer() != nullptr)
                {
                    context.GetCommandList().SetUniformBuffer(rhi::RhiShaderStage::Fragment, 1, *materialResource->GetUniformBuffer(), 0);
                    return;
                }

                EnsureDefaultMaterialBuffer(context.GetDevice());
                context.GetCommandList().SetUniformBuffer(rhi::RhiShaderStage::Fragment, 1, *defaultMaterialUniformBuffer_, 0);
            }

            void EnsureDefaultMaterialBuffer(rhi::RhiDevice& device)
            {
                if (defaultMaterialUniformBuffer_ != nullptr)
                {
                    return;
                }

                RTMaterialUniformData uniformData = {};
                defaultMaterialUniformBuffer_ =
                    device.CreateBuffer(MakeUniformBufferDesc(sizeof(RTMaterialUniformData), &uniformData, "OpaqueSceneDefaultMaterialUniformBuffer"));
                VE_ASSERT_MESSAGE(defaultMaterialUniformBuffer_ != nullptr, "OpaqueScenePass failed to create default material uniform buffer.");
            }

            [[nodiscard]] rhi::RhiFormat ResolveTargetFormat(const RenderPassContext& context) const noexcept
            {
                if (target_.colorTexture != nullptr)
                {
                    return target_.colorTexture->GetDesc().colorFormat;
                }

                VE_ASSERT(context.GetFrameData().mainSwapchain != nullptr);
                return context.GetFrameData().mainSwapchain->GetColorFormat();
            }

            RendererRenderTarget target_;
            rhi::RhiFillMode fillMode_ = rhi::RhiFillMode::Solid;
            std::unique_ptr<rhi::RhiPipelineState> pipelineState_;
            std::unique_ptr<rhi::RhiBuffer> defaultMaterialUniformBuffer_;
            std::vector<std::unique_ptr<rhi::RhiBuffer>> frameUniformBuffers_;
            rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
            rhi::RhiFillMode pipelineFillMode_ = rhi::RhiFillMode::Solid;
            bool pipelineDepthEnabled_ = false;
        };

    } // namespace

    void BaseRenderer::RenderScene(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_RENDER_THREAD();

        BeginSceneRender(frameData);
        ExecutePassesInOrder();
        EndSceneRender();
    }

    bool BaseRenderer::IsFrameActive() const noexcept
    {
        return rendererData_.active;
    }

    const RendererData& BaseRenderer::GetRendererData() const noexcept
    {
        return rendererData_;
    }

    void BaseRenderer::SetScene(std::shared_ptr<RTScene> scene) noexcept
    {
        scene_ = std::move(scene);
    }

    void BaseRenderer::SetOverrideCamera(std::shared_ptr<RTCamera> camera) noexcept
    {
        overrideCamera_ = std::move(camera);
    }

    void BaseRenderer::SetFillMode(rhi::RhiFillMode fillMode) noexcept
    {
        fillMode_ = fillMode;
    }

    std::shared_ptr<RTScene> BaseRenderer::GetScene() const noexcept
    {
        return scene_;
    }

    void BaseRenderer::AddRenderPass(std::unique_ptr<RenderPass> pass)
    {
        VE_ASSERT_MESSAGE(!rendererData_.active, "BaseRenderer::AddRenderPass requires no active renderer.");
        VE_ASSERT_MESSAGE(pass != nullptr, "BaseRenderer::AddRenderPass requires a valid pass.");
        passes_.push_back(std::move(pass));
    }

    void BaseRenderer::ClearRenderPasses() noexcept
    {
        VE_ASSERT_MESSAGE(!rendererData_.active, "BaseRenderer::ClearRenderPasses requires no active renderer.");
        passes_.clear();
    }

    void BaseRenderer::RefreshRendererSceneData() noexcept
    {
        rendererData_.scene = scene_;
        rendererData_.camera = overrideCamera_ != nullptr || scene_ == nullptr ? overrideCamera_ : FindFrameCamera(*scene_);
        rendererData_.clearColor = rendererData_.camera != nullptr ? rendererData_.camera->GetDesc().clearColor : ResolveFrameClearColor(scene_);
        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = false;
        rendererData_.renderPassOpen = false;
    }

    void BaseRenderer::UpdateRenderWorld()
    {
        // Render world updates will consume scene snapshots here once the render proxy layer grows.
    }

    void BaseRenderer::BuildVisibleDrawLists()
    {
        // Visibility and batching stay here so concrete renderers only choose their pass topology.
    }

    void BaseRenderer::BeginSceneRender(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_MESSAGE(!rendererData_.active, "BaseRenderer::BeginSceneRender requires no active renderer.");
        VE_ASSERT(frameData.device != nullptr);
        VE_ASSERT(frameData.commandList != nullptr);
        VE_ASSERT(frameData.mainSwapchain != nullptr);
        VE_ASSERT(frameData.shaderManager != nullptr);

        frameRenderData_ = &frameData;
        RefreshRendererSceneData();
        UpdateRenderWorld();
        BuildVisibleDrawLists();

        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = true;
        rendererData_.renderPassOpen = false;
    }

    void BaseRenderer::ExecutePassesInOrder()
    {
        VE_ASSERT_MESSAGE(rendererData_.active, "BaseRenderer::ExecutePassesInOrder requires an active renderer.");
        VE_ASSERT_MESSAGE(frameRenderData_ != nullptr, "BaseRenderer::ExecutePassesInOrder requires active frame data.");
        VE_ASSERT_MESSAGE(frameRenderData_->commandList != nullptr, "BaseRenderer::ExecutePassesInOrder requires an active command list.");

        while (rendererData_.activeRenderPassIndex < passes_.size())
        {
            std::unique_ptr<RenderPass>& pass = passes_[rendererData_.activeRenderPassIndex];
            VE_ASSERT(pass != nullptr);

            RenderPassData passData = BuildPassData(*pass);
            if (!BeginPass(passData))
            {
                return;
            }

            RenderPassContext passContext(*frameRenderData_, *this, passData);
            pass->Execute(passContext);

            frameRenderData_->commandList->EndRenderPass();
            rendererData_.renderPassOpen = false;
            ++rendererData_.activeRenderPassIndex;
        }
    }

    void BaseRenderer::EndSceneRender()
    {
        VE_ASSERT_MESSAGE(rendererData_.active, "BaseRenderer::EndSceneRender requires an active scene render.");
        VE_ASSERT(frameRenderData_ != nullptr);
        VE_ASSERT(frameRenderData_->commandList != nullptr);

        if (rendererData_.renderPassOpen)
        {
            frameRenderData_->commandList->EndRenderPass();
            rendererData_.renderPassOpen = false;
        }

        frameRenderData_ = nullptr;
        rendererData_.activeRenderPassIndex = 0;
        rendererData_.active = false;
    }

    RenderPassData BaseRenderer::BuildPassData(RenderPass& pass)
    {
        VE_ASSERT(frameRenderData_ != nullptr);
        RenderPassBuilder builder;
        builder.Reset(pass.GetName(), *frameRenderData_, rendererData_);
        pass.Setup(builder);

        RenderPassData passData = {};
        passData.renderPassDesc = builder.GetRenderPassDesc();
        passData.viewport = builder.GetViewport();
        passData.scissorRect = builder.GetScissor();
        return passData;
    }

    bool BaseRenderer::BeginPass(const RenderPassData& passData)
    {
        VE_ASSERT(rendererData_.active);
        VE_ASSERT(frameRenderData_ != nullptr);
        VE_ASSERT(frameRenderData_->commandList != nullptr);
        VE_ASSERT(frameRenderData_->mainSwapchain != nullptr);
        VE_ASSERT(rendererData_.activeRenderPassIndex < passes_.size());
        VE_ASSERT(!rendererData_.renderPassOpen);

        const bool beganRenderPass = frameRenderData_->commandList->BeginRenderPass(*frameRenderData_->mainSwapchain, passData.renderPassDesc);
        VE_ASSERT_MESSAGE(beganRenderPass, "BaseRenderer failed to begin render pass.");
        if (!beganRenderPass)
        {
            return false;
        }

        frameRenderData_->commandList->SetViewport(passData.viewport);
        frameRenderData_->commandList->SetScissor(passData.scissorRect);
        rendererData_.renderPassOpen = true;
        return true;
    }

    ForwardRenderer::ForwardRenderer(ForwardRendererDesc desc)
    {
        SetScene(std::move(desc.scene));
        SetOverrideCamera(std::move(desc.camera));
        SetFillMode(desc.fillMode);
        if (desc.addOpaquePass)
        {
            AddRenderPass(std::make_unique<OpaqueSceneRenderPass>(std::move(desc.target), desc.fillMode));
        }

        for (std::unique_ptr<RenderPass>& pass : desc.additionalPasses)
        {
            AddRenderPass(std::move(pass));
        }
    }
} // namespace ve
