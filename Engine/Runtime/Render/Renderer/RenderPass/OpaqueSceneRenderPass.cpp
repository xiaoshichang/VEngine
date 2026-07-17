#include "Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <cstdint>
#include <string>

namespace ve
{
    namespace
    {
        inline constexpr const char* OpaqueScenePassName = "OpaqueScenePass";

        struct OpaqueScenePassData
        {
            FrameGraphTextureHandle color;
            FrameGraphTextureHandle depth;
        };

        [[nodiscard]] std::shared_ptr<RTShaderResource> FindFirstShaderResource(const std::vector<std::shared_ptr<RTRenderItem>>& items) noexcept
        {
            for (const std::shared_ptr<RTRenderItem>& item : items)
            {
                if (item == nullptr)
                {
                    continue;
                }

                const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(item->GetMaterialResource());
                if (materialResource != nullptr && materialResource->GetShaderResource() != nullptr)
                {
                    return materialResource->GetShaderResource();
                }
            }
            return nullptr;
        }

        [[nodiscard]] Int32 BuildPipelineVariant(rhi::RhiFormat targetFormat, rhi::RhiFillMode fillMode, bool depthEnabled) noexcept
        {
            return static_cast<Int32>(targetFormat) | (static_cast<Int32>(fillMode) << 8) | (depthEnabled ? (1 << 16) : 0);
        }

        [[nodiscard]] std::string
        BuildPipelineName(const RTShaderResource& shaderResource, const rhi::RhiShaderModule& vertexShader, const rhi::RhiShaderModule& fragmentShader)
        {
            std::string name = OpaqueScenePassName;
            name += ":";
            name += shaderResource.GetDesc().name;
            name += ":VS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&vertexShader));
            name += ":FS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&fragmentShader));
            return name;
        }
    } // namespace

    OpaqueSceneRenderPass::OpaqueSceneRenderPass(OpaqueSceneRenderPassInitParam initParam)
        : initParam_(initParam)
    {
    }

    void OpaqueSceneRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        frameGraph.AddRasterPass<OpaqueScenePassData>(
            OpaqueScenePassName,
            [this, &graphData](FrameGraphBuilder& builder, OpaqueScenePassData& passData)
            {
                rhi::RhiColor clearColor = {};
                if (builder.GetRendererData().resolvedCamera != nullptr)
                {
                    clearColor = builder.GetRendererData().resolvedCamera->GetClearColor();
                }

                passData.color = builder.WriteColorAttachment(graphData.color, initParam_.colorLoadAction, clearColor);
                graphData.color = passData.color;

                if (graphData.depth.IsValid())
                {
                    passData.depth = builder.WriteDepthAttachment(graphData.depth, rhi::RhiLoadAction::Clear);
                    graphData.depth = passData.depth;
                }
            },
            [this](const OpaqueScenePassData&, RenderPassContext& context) { return Draw(context); });
    }

    ErrorCode OpaqueSceneRenderPass::Draw(RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        const std::vector<std::shared_ptr<RTRenderItem>>& items = context.rendererData.opaqueItems;
        if (items.empty())
        {
            return ErrorCode::None;
        }
        if (context.rendererData.scene == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        const ErrorCode pipelineResult = EnsurePipeline(context);
        if (pipelineResult != ErrorCode::None)
        {
            return pipelineResult;
        }

        rhi::RhiCommandList& commandList = context.commandList;
        commandList.SetPipeline(*pipelineState_);

        const UniformBufferAllocation frameUniform = context.frameData.GetFrameUniform(*context.rendererData.scene);
        const rhi::RhiRenderArea& renderArea = context.executionInfo.renderArea;
        const UniformBufferAllocation viewUniform =
            context.frameData.GetViewUniform(context.rendererData.resolvedCamera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *frameUniform.buffer, frameUniform.offset, frameUniform.size);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);

        for (SizeT itemIndex = 0; itemIndex < items.size(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem>& item = items[itemIndex];
            if (item == nullptr)
            {
                VE_LOG_WARN("%s skipping null render item at queue index %zu.", OpaqueScenePassName, itemIndex);
                continue;
            }

            const auto meshResource = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
            if (meshResource == nullptr || meshResource->GetVertexBuffer() == nullptr)
            {
                VE_LOG_WARN("%s skipping render item at queue index %zu due to missing mesh data.", OpaqueScenePassName, itemIndex);
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
        return ErrorCode::None;
    }

    ErrorCode OpaqueSceneRenderPass::EnsurePipeline(RenderPassContext& context)
    {
        std::shared_ptr<RTShaderResource> shaderResource = FindFirstShaderResource(context.rendererData.opaqueItems);
        VE_ASSERT_ALWAYS_MESSAGE(shaderResource != nullptr, "Opaque scene pass requires an initialized material shader resource.");
        if (shaderResource == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        rhi::RhiShaderModule* vertexShader = shaderResource->GetVertexShader();
        rhi::RhiShaderModule* fragmentShader = shaderResource->GetFragmentShader();
        if (vertexShader == nullptr || fragmentShader == nullptr)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Opaque scene pass material shader resource is not initialized.");
            return ErrorCode::InvalidState;
        }

        const rhi::RhiFormat targetFormat = context.executionInfo.colorFormat;
        const bool depthEnabled = context.executionInfo.depthEnabled;
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineFillMode_ == initParam_.fillMode &&
            pipelineDepthEnabled_ == depthEnabled && pipelineShaderResource_.lock() == shaderResource)
        {
            return ErrorCode::None;
        }

        ShaderManager* shaderManager = context.frameData.shaderManager;
        if (shaderManager == nullptr)
        {
            return ErrorCode::InvalidState;
        }

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
        pipelineDesc.debugName = OpaqueScenePassName;

        pipelineState_ = shaderManager->GetOrCreateGraphicsPipeline(context.device,
                                                                    GraphicsPipelineID{BuildPipelineName(*shaderResource, *vertexShader, *fragmentShader),
                                                                                       BuildPipelineVariant(targetFormat, initParam_.fillMode, depthEnabled)},
                                                                    pipelineDesc);
        if (pipelineState_ == nullptr)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Opaque scene pass failed to create its graphics pipeline.");
            return ErrorCode::PlatformError;
        }

        pipelineColorFormat_ = targetFormat;
        pipelineFillMode_ = initParam_.fillMode;
        pipelineDepthEnabled_ = depthEnabled;
        pipelineShaderResource_ = shaderResource;
        return ErrorCode::None;
    }

    bool OpaqueSceneRenderPass::BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item)
    {
        const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(item.GetMaterialResource());
        if (materialResource != nullptr && materialResource->GetUniformBuffer() != nullptr)
        {
            context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment,
                                                 3,
                                                 *materialResource->GetUniformBuffer(),
                                                 materialResource->GetUniformBufferOffset(),
                                                 materialResource->GetUniformBufferSize());
            return true;
        }

        VE_ASSERT_ALWAYS_MESSAGE(false, "Opaque scene pass requires a material resource with an initialized uniform buffer.");
        return false;
    }

} // namespace ve
