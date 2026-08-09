#include "Engine/Render/PBR/HdrToneMappingPass.h"
#include "Engine/Render/PBR/HdrColorPipeline.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Render/FrameUniformAllocator.h"
#include "Engine/Runtime/Resource/BuiltInShaderLibrary.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

namespace ve::pbr
{
    namespace
    {
        struct ToneMappingPassData
        {
            FrameGraphTextureHandle source;
            FrameGraphTextureHandle destination;
        };

        [[noreturn]] void FailToneMapping(const char* message)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, message);
            std::terminate();
        }
    } // namespace

    HdrToneMappingPass::HdrToneMappingPass(HdrSettings settings)
        : settings_(SanitizeHdrSettings(settings))
    {
    }

    void HdrToneMappingPass::SetSettings(HdrSettings settings) noexcept
    {
        settings_ = SanitizeHdrSettings(settings);
    }

    const HdrSettings& HdrToneMappingPass::GetSettings() const noexcept
    {
        return settings_;
    }

    void HdrToneMappingPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        if (!graphData.swapchainColor.IsValid() || graphData.views.empty() || !graphData.views.front().color.IsValid())
        {
            FailToneMapping("HDR tone mapping requires a scene color and swapchain color.");
        }

        frameGraph.AddRasterPass<ToneMappingPassData>(
            "HdrToneMappingPass",
            [&graphData](FrameGraphBuilder& builder, ToneMappingPassData& passData)
            {
                passData.source = builder.Read(graphData.views.front().color);
                passData.destination = builder.WriteColorAttachment(graphData.swapchainColor, rhi::RhiLoadAction::Clear);
                graphData.swapchainColor = passData.destination;
            },
            [this](const ToneMappingPassData& passData, const FrameGraphPassResources& resources, RenderPassContext& context)
            {
                const ResolvedFrameGraphTexture source = resources.GetTexture(passData.source);
                if (source.texture == nullptr)
                {
                    FailToneMapping("HDR tone mapping source texture is unavailable.");
                }
                if (!IsHdrFormat(source.texture->GetFormat()))
                {
                    FailToneMapping("HDR tone mapping source texture must use a floating-point HDR format.");
                }
                if (context.frameData.builtInShaderResources == nullptr || context.frameData.builtInShaderResources->hdrToneMapping == nullptr)
                {
                    // Loose Player runs have no asset provider, so the cleared swapchain remains the valid fallback.
                    return;
                }
                EnsurePipeline(context);
                if (pipeline_.pipeline == nullptr || sampler_ == nullptr)
                {
                    FailToneMapping("HDR tone mapping pipeline or sampler is unavailable.");
                }

                const Vector4 hdrSettings(settings_.exposure, static_cast<Float32>(settings_.toneMapping), settings_.whitePoint, 0.0f);
                const UniformBufferAllocation constants = context.frameData.UploadUniform(&hdrSettings, sizeof(hdrSettings));
                if (constants.buffer == nullptr)
                {
                    FailToneMapping("HDR tone mapping constant upload failed.");
                }
                context.commandList.SetPipeline(*pipeline_.pipeline);
                context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *constants.buffer, constants.offset, constants.size);
                context.commandList.SetTexture(rhi::RhiShaderStage::Fragment, 0, *source.texture);
                context.commandList.SetSampler(rhi::RhiShaderStage::Fragment, 0, *sampler_);
                context.commandList.Draw(3, 0);
            });
    }

    void HdrToneMappingPass::EnsurePipeline(RenderPassContext& context)
    {
        const BuiltInShaderResources* shaders = context.frameData.builtInShaderResources;
        if (shaders == nullptr || shaders->hdrToneMapping == nullptr)
        {
            FailToneMapping("HDR tone mapping shader resource is unavailable.");
        }
        const RTShaderPass* shaderPass = shaders->hdrToneMapping->GetPass("Color");
        if (shaderPass == nullptr || shaderPass->GetVertexShader() == nullptr || shaderPass->GetFragmentShader() == nullptr)
        {
            FailToneMapping("HDR tone mapping shader pass is incomplete.");
        }

        if (pipeline_.pipeline != nullptr && pipeline_.colorFormat == context.executionInfo.colorFormat &&
            pipeline_.shader.lock() == shaders->hdrToneMapping && sampler_ != nullptr)
        {
            return;
        }

        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 0},
            {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Fragment, 0},
            {rhi::RhiPipelineResourceKind::Sampler, rhi::RhiShaderStage::Fragment, 0},
        };
        rhi::RhiGraphicsPipelineDesc desc = {};
        desc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        desc.rasterizerState = rhi::StaticRenderStates::SolidNoCullRasterizer;
        desc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
        desc.boundShaderState.vertexShader = shaderPass->GetVertexShader();
        desc.boundShaderState.fragmentShader = shaderPass->GetFragmentShader();
        desc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
        desc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        desc.colorAttachmentCount = 1;
        desc.colorFormat = context.executionInfo.colorFormat;
        desc.debugName = "HdrToneMappingPipeline";
        if (context.frameData.pipelineManager == nullptr)
        {
            FailToneMapping("HDR tone mapping pipeline manager is unavailable.");
        }
        std::string pipelineName = "HdrToneMappingPipeline:";
        pipelineName += std::to_string(reinterpret_cast<std::uintptr_t>(shaderPass->GetVertexShader()));
        pipelineName += ":";
        pipelineName += std::to_string(reinterpret_cast<std::uintptr_t>(shaderPass->GetFragmentShader()));
        pipeline_.pipeline = context.frameData.pipelineManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{std::move(pipelineName), static_cast<Int32>(context.executionInfo.colorFormat)}, desc);
        pipeline_.colorFormat = context.executionInfo.colorFormat;
        pipeline_.shader = shaders->hdrToneMapping;
        sampler_ = context.device.CreateSampler(rhi::StaticRenderStates::BilinearClampSampler);
    }
} // namespace ve::pbr
