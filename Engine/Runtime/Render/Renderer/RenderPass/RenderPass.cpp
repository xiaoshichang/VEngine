#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

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
        [[nodiscard]] rhi::RhiDevice& ResolveDevice(const FrameRenderPipelineData& frameData) noexcept
        {
            VE_ASSERT(frameData.device != nullptr);
            return *frameData.device;
        }

        [[nodiscard]] std::shared_ptr<RTShaderResource>
        FindFirstShaderResource(const std::vector<std::shared_ptr<RTRenderItem>>& items) noexcept
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

        [[nodiscard]] Int32 BuildPipelineVariant(rhi::RhiFormat targetFormat,
                                                 rhi::RhiFillMode fillMode,
                                                 bool depthEnabled,
                                                 bool transparent) noexcept
        {
            return static_cast<Int32>(targetFormat) | (static_cast<Int32>(fillMode) << 8) | (depthEnabled ? (1 << 16) : 0) |
                   (transparent ? (1 << 17) : 0);
        }

        [[nodiscard]] std::string BuildPipelineName(const SceneRenderPassSettings& settings,
                                                    const RTShaderResource& shaderResource,
                                                    const rhi::RhiShaderModule& vertexShader,
                                                    const rhi::RhiShaderModule& fragmentShader)
        {
            std::string name = settings.passName;
            name += ":";
            name += shaderResource.GetDesc().name;
            name += ":VS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&vertexShader));
            name += ":FS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&fragmentShader));
            return name;
        }
    } // namespace

    RenderPassContext::RenderPassContext(RenderPassContextInitParam initParam) noexcept
        : frameData(initParam.frameData)
        , rendererData(initParam.rendererData)
        , passData(initParam.passData)
        , device(ResolveDevice(initParam.frameData))
        , commandList(initParam.frameData.GetCommandList())
    {
    }

    SceneRenderPassExecutor::SceneRenderPassExecutor(SceneRenderPassSettings settings)
        : settings_(std::move(settings))
    {
    }

    ErrorCode SceneRenderPassExecutor::Draw(RenderPassContext& context, const std::vector<std::shared_ptr<RTRenderItem>>& items)
    {
        VE_ASSERT_RENDER_THREAD();
        if (items.empty())
        {
            return ErrorCode::None;
        }

        if (context.rendererData.scene == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        const ErrorCode pipelineResult = EnsurePipeline(context, items);
        if (pipelineResult != ErrorCode::None)
        {
            return pipelineResult;
        }

        rhi::RhiCommandList& commandList = context.commandList;
        commandList.SetPipeline(*pipelineState_);

        const UniformBufferAllocation frameUniform = context.frameData.GetFrameUniform(*context.rendererData.scene);
        const rhi::RhiRenderArea& renderArea = context.passData.renderPassDesc.renderArea;
        const UniformBufferAllocation viewUniform =
            context.frameData.GetViewUniform(context.rendererData.resolvedCamera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *frameUniform.buffer, frameUniform.offset, frameUniform.size);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);

        for (SizeT itemIndex = 0; itemIndex < items.size(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem>& item = items[itemIndex];
            if (item == nullptr)
            {
                VE_LOG_WARN("%s skipping null render item at queue index %zu.", settings_.passName.c_str(), itemIndex);
                continue;
            }

            const auto meshResource = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
            if (meshResource == nullptr || meshResource->GetVertexBuffer() == nullptr)
            {
                VE_LOG_WARN("%s skipping render item at queue index %zu due to missing mesh data.", settings_.passName.c_str(), itemIndex);
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

    ErrorCode SceneRenderPassExecutor::EnsurePipeline(RenderPassContext& context,
                                                      const std::vector<std::shared_ptr<RTRenderItem>>& items)
    {
        std::shared_ptr<RTShaderResource> shaderResource = FindFirstShaderResource(items);
        VE_ASSERT_ALWAYS_MESSAGE(shaderResource != nullptr, "Scene render pass requires an initialized material shader resource.");
        if (shaderResource == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        rhi::RhiShaderModule* vertexShader = shaderResource->GetVertexShader();
        rhi::RhiShaderModule* fragmentShader = shaderResource->GetFragmentShader();
        if (vertexShader == nullptr || fragmentShader == nullptr)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Scene render pass material shader resource is not initialized.");
            return ErrorCode::InvalidState;
        }

        const rhi::RhiFormat targetFormat = ResolveTargetFormat(context);
        const bool depthEnabled = context.passData.renderPassDesc.hasDepthStencilAttachment;
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineFillMode_ == settings_.fillMode &&
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
        pipelineDesc.blendState = settings_.transparent ? rhi::StaticRenderStates::AlphaBlend : rhi::StaticRenderStates::OpaqueBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
        pipelineDesc.rasterizerState.fillMode = settings_.fillMode;
        if (!depthEnabled)
        {
            pipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
        }
        else
        {
            pipelineDesc.depthStencilState =
                settings_.transparent ? rhi::StaticRenderStates::DepthReadOnlyLessEqual : rhi::StaticRenderStates::DepthReadWriteLessEqual;
        }
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.boundShaderState.fragmentShader = fragmentShader;
        pipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        pipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 2;
        pipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(RTMeshVertex);
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.debugName = settings_.passName.c_str();

        pipelineState_ = shaderManager->GetOrCreateGraphicsPipeline(
            context.device,
            GraphicsPipelineID{BuildPipelineName(settings_, *shaderResource, *vertexShader, *fragmentShader),
                               BuildPipelineVariant(targetFormat, settings_.fillMode, depthEnabled, settings_.transparent)},
            pipelineDesc);
        if (pipelineState_ == nullptr)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Scene render pass failed to create its graphics pipeline.");
            return ErrorCode::PlatformError;
        }

        pipelineColorFormat_ = targetFormat;
        pipelineFillMode_ = settings_.fillMode;
        pipelineDepthEnabled_ = depthEnabled;
        pipelineShaderResource_ = shaderResource;
        return ErrorCode::None;
    }

    bool SceneRenderPassExecutor::BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item)
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

        VE_ASSERT_ALWAYS_MESSAGE(false, "Scene render pass requires a material resource with an initialized uniform buffer.");
        return false;
    }

    rhi::RhiFormat SceneRenderPassExecutor::ResolveTargetFormat(const RenderPassContext& context) const noexcept
    {
        if (context.passData.renderPassDesc.colorAttachmentCount > 0 && context.passData.renderPassDesc.colorAttachments[0].texture != nullptr)
        {
            return context.passData.renderPassDesc.colorAttachments[0].texture->GetFormat();
        }
        VE_ASSERT(context.frameData.mainSwapchain != nullptr);
        return context.frameData.mainSwapchain->GetColorFormat();
    }
} // namespace ve
