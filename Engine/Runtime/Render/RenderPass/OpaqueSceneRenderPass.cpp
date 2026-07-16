#include "Engine/Runtime/Render/RenderPass/OpaqueSceneRenderPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <cstdint>
#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] std::shared_ptr<RTShaderResource> FindFirstShaderResource(const RTScene& scene) noexcept
        {
            for (SizeT itemIndex = 0; itemIndex < scene.GetRenderItemCount(); ++itemIndex)
            {
                const std::shared_ptr<RTRenderItem> item = scene.GetRenderItem(itemIndex);
                if (item == nullptr)
                {
                    continue;
                }

                const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(item->GetMaterialResource());
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

        [[nodiscard]] Int32 BuildOpaquePipelineVariant(rhi::RhiFormat targetFormat, rhi::RhiFillMode fillMode, bool depthEnabled) noexcept
        {
            return static_cast<Int32>(targetFormat) | (static_cast<Int32>(fillMode) << 8) | (depthEnabled ? (1 << 16) : 0);
        }

        [[nodiscard]] std::string
        BuildOpaquePipelineName(const RTShaderResource& shaderResource, const rhi::RhiShaderModule& vertexShader, const rhi::RhiShaderModule& fragmentShader)
        {
            std::string name = "OpaqueScenePipeline:";
            name += shaderResource.GetDesc().name;
            name += ":VS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&vertexShader));
            name += ":FS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&fragmentShader));
            return name;
        }

        [[nodiscard]] rhi::RhiColor ResolvePassClearColor(const RendererData& rendererData) noexcept
        {
            VE_ASSERT_ALWAYS_MESSAGE(rendererData.resolvedCamera != nullptr, "OpaqueScenePass requires a resolved camera for clear color.");
            if (rendererData.resolvedCamera == nullptr)
            {
                return rhi::RhiColor{};
            }

            return rendererData.resolvedCamera->GetClearColor();
        }
    } // namespace

    OpaqueSceneRenderPass::OpaqueSceneRenderPass(OpaqueSceneRenderPassInitParam initParam)
        : initParam_(std::move(initParam))
    {
    }

    const char* OpaqueSceneRenderPass::GetName() const noexcept
    {
        return "OpaqueScenePass";
    }

    void OpaqueSceneRenderPass::Setup(RenderPassBuilder& builder)
    {
        const rhi::RhiColor clearColor = ResolvePassClearColor(builder.rendererData);
        if (initParam_.target.colorTexture != nullptr && initParam_.target.colorTexture->GetTexture() != nullptr)
        {
            builder.AddTextureColorAttachment(
                *initParam_.target.colorTexture->GetTexture(), rhi::RhiLoadAction::Clear, initParam_.target.colorStoreAction, clearColor);
            if (initParam_.target.colorTexture->GetDepthTexture() != nullptr)
            {
                builder.SetDepthStencilAttachment(*initParam_.target.colorTexture->GetDepthTexture(),
                                                  rhi::RhiLoadAction::Clear,
                                                  rhi::RhiStoreAction::Store,
                                                  rhi::RhiDepthStencilClearValue{});
            }
            return;
        }

        builder.AddSwapchainColorAttachment(rhi::RhiLoadAction::Clear, initParam_.target.colorStoreAction, clearColor);
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

        const UniformBufferAllocation frameUniform = context.frameData.GetFrameUniform(*scene);
        const UniformBufferAllocation viewUniform = context.frameData.GetViewUniform(context.rendererData.resolvedCamera.get());
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *frameUniform.buffer, frameUniform.offset, frameUniform.size);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
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

            const UniformBufferAllocation objectUniform = context.frameData.GetObjectUniform(*item);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *objectUniform.buffer, objectUniform.offset, objectUniform.size);

            if (!BindMaterialUniform(context, *item))
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
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineFillMode_ == initParam_.fillMode &&
            pipelineDepthEnabled_ == depthEnabled && sameResourceShader)
        {
            return;
        }

        ShaderManager* shaderManager = context.frameData.shaderManager;
        VE_ASSERT_MESSAGE(shaderManager != nullptr, "OpaqueScenePass requires a ShaderManager.");
        if (shaderManager == nullptr)
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
        pipelineDesc.rasterizerState.fillMode = initParam_.fillMode;
        pipelineDesc.depthStencilState = depthEnabled ? rhi::StaticRenderStates::DepthReadWriteLessEqual : rhi::StaticRenderStates::DepthDisabled;
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.boundShaderState.fragmentShader = fragmentShader;
        pipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        pipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 2;
        pipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(RTMeshVertex);
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.debugName = "OpaqueScenePipeline";

        pipelineState_ =
            shaderManager->GetOrCreateGraphicsPipeline(device,
                                                       GraphicsPipelineID{BuildOpaquePipelineName(*shaderResource, *vertexShader, *fragmentShader),
                                                                          BuildOpaquePipelineVariant(targetFormat, initParam_.fillMode, depthEnabled)},
                                                       pipelineDesc);
        VE_ASSERT_MESSAGE(pipelineState_ != nullptr, "OpaqueScenePass failed to create pipeline state.");
        pipelineColorFormat_ = targetFormat;
        pipelineFillMode_ = initParam_.fillMode;
        pipelineDepthEnabled_ = depthEnabled;
        pipelineShaderResource_ = shaderResource;
    }

    bool OpaqueSceneRenderPass::BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item)
    {
        const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(item.GetMaterialResource());
        if (materialResource != nullptr && materialResource->GetUniformBuffer() != nullptr)
        {
            const rhi::RhiBuffer* uniformBuffer = materialResource->GetUniformBuffer();
            context.commandList.SetUniformBuffer(
                rhi::RhiShaderStage::Fragment, 3, *uniformBuffer, materialResource->GetUniformBufferOffset(), materialResource->GetUniformBufferSize());
            return true;
        }

        VE_ASSERT_ALWAYS_MESSAGE(false, "OpaqueScenePass requires a material resource with an initialized uniform buffer.");
        return false;
    }

    rhi::RhiFormat OpaqueSceneRenderPass::ResolveTargetFormat(const RenderPassContext& context) const noexcept
    {
        if (initParam_.target.colorTexture != nullptr)
        {
            return initParam_.target.colorTexture->GetDesc().colorFormat;
        }

        VE_ASSERT(context.frameData.mainSwapchain != nullptr);
        return context.frameData.mainSwapchain->GetColorFormat();
    }
} // namespace ve
