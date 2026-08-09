#include "Editor/RenderPass/EditorGizmoRenderPass.h"

#include "Editor/Core/EditorBuiltinResources.h"
#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Resource/BuiltInShaderLibrary.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>
#include <exception>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] UInt64 GrowBufferCapacity(UInt64 currentCapacity, UInt64 requiredCapacity) noexcept
        {
            constexpr UInt64 InitialCapacity = 4096;
            UInt64 capacity = std::max(currentCapacity, InitialCapacity);
            while (capacity < requiredCapacity)
            {
                if (capacity > std::numeric_limits<UInt64>::max() / 2)
                {
                    return requiredCapacity;
                }
                capacity *= 2;
            }
            return capacity;
        }

        [[nodiscard]] rhi::RhiBufferDesc MakeDynamicVertexBufferDesc(UInt64 size, const char* debugName) noexcept
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = rhi::RhiBufferUsage::Vertex;
            desc.memoryUsage = rhi::RhiBufferMemoryUsage::CpuToGpu;
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

        [[noreturn]] void FailEditorGizmoPass(const std::string& message)
        {
            VE_LOG_ERROR("EditorGizmoRenderPass: {}", message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message.c_str());
            std::terminate();
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

    void EditorGizmoRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex)
    {
        if (viewIndex >= graphData.views.size() || viewIndex >= frameGraph.GetRendererData().views.size() || !graphData.views[viewIndex].color.IsValid())
        {
            FailEditorGizmoPass("registration requires a valid indexed view and color target.");
        }
        struct GizmoPassData
        {
            UInt32 viewIndex = 0;
            FrameGraphTextureHandle color;
        };

        frameGraph.AddRasterPass<GizmoPassData>(
            "EditorGizmoPass",
            [&graphData, viewIndex](FrameGraphBuilder& builder, GizmoPassData& passData)
            {
                RendererViewFrameGraphData& viewGraphData = graphData.views[viewIndex];
                passData.viewIndex = viewIndex;
                passData.color = builder.WriteColorAttachment(viewGraphData.color, rhi::RhiLoadAction::Load);
                viewGraphData.color = passData.color;
            },
            [this](const GizmoPassData& passData, RenderPassContext& context)
            {
                Execute(context, passData.viewIndex);
            });
    }

    void EditorGizmoRenderPass::Execute(RenderPassContext& context, UInt32 viewIndex)
    {
        VE_ASSERT_RENDER_THREAD();
        if (viewIndex >= context.rendererData.views.size())
        {
            FailEditorGizmoPass("execution references an out-of-bounds renderer view.");
        }
        if (initParam_.drawList == nullptr)
        {
            FailEditorGizmoPass("execution requires a gizmo draw list.");
        }
        if (initParam_.resources == nullptr)
        {
            FailEditorGizmoPass("execution requires persistent gizmo render resources.");
        }
        if (initParam_.drawList->lines.empty() && initParam_.drawList->icons.empty())
        {
            return;
        }
        const RendererViewData& viewData = context.GetView(viewIndex);
        if (viewData.view.camera == nullptr)
        {
            FailEditorGizmoPass("queued gizmo draws require a camera.");
        }

        EnsurePipeline(context);
        EditorGizmoRenderResources& resources = *initParam_.resources;
        if (resources.linePipelineState_ == nullptr || resources.iconPipelineState_ == nullptr)
        {
            FailEditorGizmoPass("execution requires initialized line and icon pipelines.");
        }
        if (!initParam_.drawList->icons.empty())
        {
            EnsureIconResources(context);
        }
        UploadFrameResources(context);
        const rhi::RhiRenderArea& renderArea = context.executionInfo.renderArea;
        const UniformBufferAllocation viewUniform =
            context.frameData.GetViewUniform(viewData.view.camera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});
        if (viewUniform.buffer == nullptr)
        {
            FailEditorGizmoPass("execution failed to allocate the required view uniform.");
        }

        rhi::RhiCommandList& commandList = context.commandList;
        if (uploadedIconVertexCount_ > 0)
        {
            commandList.SetPipeline(*resources.iconPipelineState_);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
            commandList.SetTexture(rhi::RhiShaderStage::Fragment, 0, *resources.iconAtlasTexture_);
            commandList.SetSampler(rhi::RhiShaderStage::Fragment, 0, *resources.iconSampler_);
            commandList.SetVertexBuffer(0, *iconVertexBuffer_, sizeof(EditorGizmoIconVertex), 0);
            commandList.Draw(static_cast<UInt32>(uploadedIconVertexCount_), 0);
        }

        if (uploadedLineVertexCount_ > 0)
        {
            commandList.SetPipeline(*resources.linePipelineState_);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
            commandList.SetVertexBuffer(0, *lineVertexBuffer_, sizeof(EditorGizmoVertex), 0);
            commandList.Draw(static_cast<UInt32>(uploadedLineVertexCount_), 0);
        }
    }

    void EditorGizmoRenderPass::EnsurePipeline(RenderPassContext& context)
    {
        EditorGizmoRenderResources& resources = *initParam_.resources;
        const rhi::RhiFormat targetFormat = context.executionInfo.colorFormat;
        if (resources.linePipelineState_ != nullptr && resources.iconPipelineState_ != nullptr && resources.pipelineColorFormat_ == targetFormat)
        {
            return;
        }

        RHIPipelineManager* pipelineManager = context.frameData.pipelineManager;
        if (pipelineManager == nullptr)
        {
            FailEditorGizmoPass("pipeline creation requires the frame RHIPipelineManager.");
        }

        if (context.frameData.builtInShaderResources == nullptr || context.frameData.builtInShaderResources->editorGizmoLine == nullptr ||
            context.frameData.builtInShaderResources->editorGizmoIcon == nullptr)
        {
            FailEditorGizmoPass("the editor gizmo shader resources are unavailable.");
        }
        const RTShaderPass* lineShaderPass = context.frameData.builtInShaderResources->editorGizmoLine->GetPass("Internal");
        const RTShaderPass* iconShaderPass = context.frameData.builtInShaderResources->editorGizmoIcon->GetPass("Internal");
        if (lineShaderPass == nullptr || iconShaderPass == nullptr || lineShaderPass->GetVertexShader() == nullptr ||
            lineShaderPass->GetFragmentShader() == nullptr || iconShaderPass->GetVertexShader() == nullptr || iconShaderPass->GetFragmentShader() == nullptr)
        {
            FailEditorGizmoPass("the editor gizmo shader passes are unavailable.");
        }

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
        linePipelineDesc.boundShaderState.vertexShader = lineShaderPass->GetVertexShader();
        linePipelineDesc.boundShaderState.fragmentShader = lineShaderPass->GetFragmentShader();
        linePipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        linePipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 2;
        linePipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(EditorGizmoVertex);
        linePipelineDesc.resourceLayout.bindings = lineResourceBindings;
        linePipelineDesc.resourceLayout.bindingCount = static_cast<UInt32>(std::size(lineResourceBindings));
        linePipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::LineList;
        linePipelineDesc.colorAttachmentCount = 1;
        linePipelineDesc.colorFormat = targetFormat;
        linePipelineDesc.debugName = "EditorGizmoLinePipeline";

        resources.linePipelineState_ = pipelineManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{"EditorGizmoLinePipeline", BuildEditorGizmoPipelineVariant(targetFormat)}, linePipelineDesc);
        if (resources.linePipelineState_ == nullptr)
        {
            FailEditorGizmoPass(BuildDeviceFailureMessage(context.device, "failed to create the line pipeline state."));
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
        iconPipelineDesc.boundShaderState.vertexShader = iconShaderPass->GetVertexShader();
        iconPipelineDesc.boundShaderState.fragmentShader = iconShaderPass->GetFragmentShader();
        iconPipelineDesc.boundShaderState.vertexDeclaration.attributes = iconVertexAttributes;
        iconPipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 3;
        iconPipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(EditorGizmoIconVertex);
        iconPipelineDesc.resourceLayout.bindings = iconResourceBindings;
        iconPipelineDesc.resourceLayout.bindingCount = static_cast<UInt32>(std::size(iconResourceBindings));
        iconPipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        iconPipelineDesc.colorAttachmentCount = 1;
        iconPipelineDesc.colorFormat = targetFormat;
        iconPipelineDesc.debugName = "EditorGizmoIconPipeline";

        resources.iconPipelineState_ = pipelineManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{"EditorGizmoIconPipeline", BuildEditorGizmoPipelineVariant(targetFormat)}, iconPipelineDesc);
        if (resources.iconPipelineState_ == nullptr)
        {
            FailEditorGizmoPass(BuildDeviceFailureMessage(context.device, "failed to create the icon pipeline state."));
        }
        resources.pipelineColorFormat_ = targetFormat;
    }

    void EditorGizmoRenderPass::EnsureIconResources(RenderPassContext& context)
    {
        EditorGizmoRenderResources& resources = *initParam_.resources;
        if (resources.iconAtlasTexture_ == nullptr)
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

            resources.iconAtlasTexture_ = context.device.CreateTexture(textureDesc);
            if (resources.iconAtlasTexture_ == nullptr)
            {
                FailEditorGizmoPass(BuildDeviceFailureMessage(context.device, "failed to create the builtin gizmo icon atlas texture."));
            }
        }

        if (resources.iconSampler_ == nullptr)
        {
            resources.iconSampler_ = context.device.CreateSampler(rhi::StaticRenderStates::BilinearClampSampler);
            if (resources.iconSampler_ == nullptr)
            {
                FailEditorGizmoPass(BuildDeviceFailureMessage(context.device, "failed to create the builtin gizmo icon sampler."));
            }
        }
    }

    void EditorGizmoRenderPass::UploadFrameResources(RenderPassContext& context)
    {
        if (initParam_.drawList == nullptr)
        {
            FailEditorGizmoPass("frame-resource upload requires a gizmo draw list.");
        }
        uploadedLineVertexCount_ = initParam_.drawList->lines.size();
        uploadedIconVertexCount_ = initParam_.drawList->icons.size();
        if (uploadedLineVertexCount_ > static_cast<SizeT>(std::numeric_limits<UInt32>::max()) ||
            uploadedIconVertexCount_ > static_cast<SizeT>(std::numeric_limits<UInt32>::max()))
        {
            FailEditorGizmoPass("draw-list vertex count exceeds the RHI draw-call range.");
        }

        EditorGizmoRenderResources& resources = *initParam_.resources;
        const UInt32 frameSlot = static_cast<UInt32>(context.frameData.frameIndex % RenderFrameContextCount);
        lineVertexBuffer_ = nullptr;
        iconVertexBuffer_ = nullptr;

        if (uploadedLineVertexCount_ > 0)
        {
            const UInt64 requiredSize = static_cast<UInt64>(uploadedLineVertexCount_ * sizeof(EditorGizmoVertex));
            if (resources.lineVertexBuffers_[frameSlot] == nullptr || resources.lineVertexBufferCapacities_[frameSlot] < requiredSize)
            {
                const UInt64 capacity = GrowBufferCapacity(resources.lineVertexBufferCapacities_[frameSlot], requiredSize);
                resources.lineVertexBuffers_[frameSlot] =
                    context.device.CreateBuffer(MakeDynamicVertexBufferDesc(capacity, "EditorGizmoLineVertexBuffer"));
                if (resources.lineVertexBuffers_[frameSlot] == nullptr)
                {
                    FailEditorGizmoPass(BuildDeviceFailureMessage(context.device, "failed to create the line vertex buffer."));
                }
                resources.lineVertexBufferCapacities_[frameSlot] = capacity;
            }
            lineVertexBuffer_ = resources.lineVertexBuffers_[frameSlot].get();
            context.device.UpdateBuffer(
                *lineVertexBuffer_, 0, initParam_.drawList->lines.data(), requiredSize, rhi::RhiBufferUpdateMode::Discard);
        }

        if (uploadedIconVertexCount_ > 0)
        {
            const UInt64 requiredSize = static_cast<UInt64>(uploadedIconVertexCount_ * sizeof(EditorGizmoIconVertex));
            if (resources.iconVertexBuffers_[frameSlot] == nullptr || resources.iconVertexBufferCapacities_[frameSlot] < requiredSize)
            {
                const UInt64 capacity = GrowBufferCapacity(resources.iconVertexBufferCapacities_[frameSlot], requiredSize);
                resources.iconVertexBuffers_[frameSlot] =
                    context.device.CreateBuffer(MakeDynamicVertexBufferDesc(capacity, "EditorGizmoIconVertexBuffer"));
                if (resources.iconVertexBuffers_[frameSlot] == nullptr)
                {
                    FailEditorGizmoPass(BuildDeviceFailureMessage(context.device, "failed to create the icon vertex buffer."));
                }
                resources.iconVertexBufferCapacities_[frameSlot] = capacity;
            }
            iconVertexBuffer_ = resources.iconVertexBuffers_[frameSlot].get();
            context.device.UpdateBuffer(
                *iconVertexBuffer_, 0, initParam_.drawList->icons.data(), requiredSize, rhi::RhiBufferUpdateMode::Discard);
        }
    }

} // namespace ve
