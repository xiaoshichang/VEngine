#include "Editor/RenderPass/SceneGridRenderPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Resource/BuiltInShaderLibrary.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        constexpr Float32 GridExtent = 1000.0f;

        struct SceneGridUniformData
        {
            Float32 gridParams[4] = {1.0f, 0.45f, 0.025f, 10.0f};
            Float32 minorColor[4] = {0.45f, 0.48f, 0.52f, 1.0f};
            Float32 majorColor[4] = {0.72f, 0.75f, 0.80f, 1.0f};
            Float32 xAxisColor[4] = {0.95f, 0.20f, 0.20f, 1.0f};
            Float32 zAxisColor[4] = {0.22f, 0.42f, 0.95f, 1.0f};
        };

        static_assert(sizeof(SceneGridUniformData) == 80);

        [[nodiscard]] rhi::RhiBufferDesc MakeBufferDesc(UInt64 size, rhi::RhiBufferUsage usage, const void* initialData, const char* debugName) noexcept
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = usage;
            desc.initialData = initialData;
            desc.debugName = debugName;
            return desc;
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

        [[noreturn]] void FailSceneGridPass(const std::string& message)
        {
            VE_LOG_ERROR("SceneGridRenderPass: {}", message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message.c_str());
            std::terminate();
        }

        [[nodiscard]] Int32 BuildSceneGridPipelineVariant(rhi::RhiFormat targetFormat, bool depthEnabled) noexcept
        {
            return static_cast<Int32>(targetFormat) | (depthEnabled ? (1 << 16) : 0);
        }

        [[nodiscard]] SceneGridUniformData BuildUniformData(const SceneGridRenderPassInitParam& initParam) noexcept
        {
            SceneGridUniformData data = {};
            data.gridParams[0] = std::max(initParam.unitSize, 0.001f);
            data.gridParams[1] = Clamp(initParam.opacity, 0.0f, 1.0f);
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

    SceneGridRenderPass::SceneGridRenderPass(SceneGridRenderPassInitParam initParam)
        : initParam_(std::move(initParam))
    {
    }

    void SceneGridRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex)
    {
        if (viewIndex >= graphData.views.size() || viewIndex >= frameGraph.GetRendererData().views.size() || !graphData.views[viewIndex].color.IsValid())
        {
            FailSceneGridPass("registration requires a valid indexed view and color target.");
        }
        struct GridPassData
        {
            UInt32 viewIndex = 0;
            FrameGraphTextureHandle color;
            FrameGraphTextureHandle depth;
        };

        frameGraph.AddRasterPass<GridPassData>(
            "SceneGridPass",
            [&graphData, viewIndex](FrameGraphBuilder& builder, GridPassData& passData)
            {
                RendererViewFrameGraphData& viewGraphData = graphData.views[viewIndex];
                passData.viewIndex = viewIndex;
                passData.color = builder.WriteColorAttachment(viewGraphData.color, rhi::RhiLoadAction::Load);
                viewGraphData.color = passData.color;
                if (viewGraphData.depth.IsValid())
                {
                    passData.depth = builder.ReadDepthAttachment(viewGraphData.depth);
                }
            },
            [this](const GridPassData& passData, RenderPassContext& context)
            {
                Execute(context, passData.viewIndex);
            });
    }

    void SceneGridRenderPass::Execute(RenderPassContext& context, UInt32 viewIndex)
    {
        VE_ASSERT_RENDER_THREAD();
        if (viewIndex >= context.rendererData.views.size())
        {
            FailSceneGridPass("execution references an out-of-bounds renderer view.");
        }
        const RendererViewData& viewData = context.GetView(viewIndex);
        if (initParam_.opacity <= 0.0f || viewData.view.camera == nullptr)
        {
            return;
        }

        EnsureResources(context);
        EnsurePipeline(context);
        if (pipelineState_ == nullptr || vertexBuffer_ == nullptr)
        {
            FailSceneGridPass("execution requires initialized pipeline and vertex-buffer resources.");
        }
        const SceneGridUniformData gridUniformData = BuildUniformData(initParam_);
        const UniformBufferAllocation gridUniform =
            context.frameData.UploadTransientUniform(&gridUniformData, sizeof(gridUniformData), "SceneGridUniform");
        const rhi::RhiRenderArea& renderArea = context.executionInfo.renderArea;
        const UniformBufferAllocation viewUniform = context.frameData.GetViewUniform(
            *viewData.view.viewState, viewData.view.camera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});
        if (gridUniform.buffer == nullptr || viewUniform.buffer == nullptr)
        {
            FailSceneGridPass("execution failed to allocate required grid or view uniforms.");
        }

        rhi::RhiCommandList& commandList = context.commandList;
        commandList.SetPipeline(*pipelineState_);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *gridUniform.buffer, gridUniform.offset, gridUniform.size);
        commandList.SetVertexBuffer(0, *vertexBuffer_, sizeof(RTMeshVertex), 0);
        commandList.Draw(12, 0);

        context.frameData.AdoptTransientRhiObject(std::shared_ptr<rhi::RhiObject>(std::move(vertexBuffer_)));
    }

    void SceneGridRenderPass::EnsureResources(RenderPassContext& context)
    {
        if (vertexBuffer_ != nullptr)
        {
            return;
        }

        const std::vector<RTMeshVertex> vertices = BuildGridPlaneVertices();
        vertexBuffer_ = context.device.CreateBuffer(
            MakeBufferDesc(static_cast<UInt64>(vertices.size() * sizeof(RTMeshVertex)), rhi::RhiBufferUsage::Vertex, vertices.data(), "SceneGridVertexBuffer"));
        if (vertexBuffer_ == nullptr)
        {
            FailSceneGridPass(BuildDeviceFailureMessage(context.device, "failed to create the grid vertex buffer."));
        }
    }

    void SceneGridRenderPass::EnsurePipeline(RenderPassContext& context)
    {
        const bool depthEnabled = context.executionInfo.depthEnabled;
        const rhi::RhiFormat targetFormat = context.executionInfo.colorFormat;
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineDepthEnabled_ == depthEnabled)
        {
            return;
        }

        RHIPipelineManager* pipelineManager = context.frameData.pipelineManager;
        if (pipelineManager == nullptr)
        {
            FailSceneGridPass("pipeline creation requires the frame RHIPipelineManager.");
        }

        if (context.frameData.builtInShaderResources == nullptr || context.frameData.builtInShaderResources->sceneGrid == nullptr)
        {
            FailSceneGridPass("the SceneGrid shader resource is unavailable.");
        }
        const RTShaderPass* shaderPass = context.frameData.builtInShaderResources->sceneGrid->GetPass("Internal");
        if (shaderPass == nullptr || shaderPass->GetVertexShader() == nullptr || shaderPass->GetFragmentShader() == nullptr)
        {
            FailSceneGridPass("the SceneGrid shader pass is unavailable.");
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
        const rhi::RhiPipelineResourceBindingDesc resourceBindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 0},
        };

        rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.blendState = rhi::StaticRenderStates::AlphaBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
        pipelineDesc.depthStencilState = depthEnabled ? rhi::StaticRenderStates::DepthReadOnlyLessEqual : rhi::StaticRenderStates::DepthDisabled;
        pipelineDesc.boundShaderState.vertexShader = shaderPass->GetVertexShader();
        pipelineDesc.boundShaderState.fragmentShader = shaderPass->GetFragmentShader();
        pipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        pipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 2;
        pipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(RTMeshVertex);
        pipelineDesc.resourceLayout.bindings = resourceBindings;
        pipelineDesc.resourceLayout.bindingCount = static_cast<UInt32>(std::size(resourceBindings));
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorAttachmentCount = 1;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.debugName = "SceneGridPipeline";

        pipelineState_ = pipelineManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{"SceneGridPipeline", BuildSceneGridPipelineVariant(targetFormat, depthEnabled)}, pipelineDesc);
        if (pipelineState_ == nullptr)
        {
            FailSceneGridPass(BuildDeviceFailureMessage(context.device, "failed to create the grid pipeline state."));
        }
        pipelineColorFormat_ = targetFormat;
        pipelineDepthEnabled_ = depthEnabled;
    }

} // namespace ve
