#include "Engine/Render/PBR/HdrToneMappingPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Render/PBR/HdrColorPipeline.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"
#include "Engine/Runtime/Render/RenderTexture.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Resource/BuiltInShaderLibrary.h"

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

        [[nodiscard]] rhi::RhiTextureUsage MakeToneMappingDestinationUsage() noexcept
        {
            return static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::Sampled) |
                                                     static_cast<UInt32>(rhi::RhiTextureUsage::RenderTarget));
        }

        [[nodiscard]] FrameGraphTextureDesc BuildToneMappingDestinationDesc(const rhi::RhiTexture& texture) noexcept
        {
            FrameGraphTextureDesc desc = {};
            desc.dimension = texture.GetDimension();
            desc.width = texture.GetWidth();
            desc.height = texture.GetHeight();
            desc.format = texture.GetFormat();
            desc.usage = MakeToneMappingDestinationUsage();
            return desc;
        }
    } // namespace

    class HdrToneMappingDrawResources final
    {
    public:
        void Draw(RenderPassContext& context, rhi::RhiTexture& source, const HdrSettings& settings)
        {
            EnsurePipeline(context);
            if (pipeline_.pipeline == nullptr || sampler_ == nullptr)
            {
                FailToneMapping("HDR tone mapping pipeline or sampler is unavailable.");
            }

            const Vector4 hdrSettings(settings.exposure, static_cast<Float32>(settings.toneMapping), settings.whitePoint, 0.0f);
            const UniformBufferAllocation constants =
                context.frameData.UploadTransientUniform(&hdrSettings, sizeof(hdrSettings), "HdrToneMappingUniform");
            if (constants.buffer == nullptr)
            {
                FailToneMapping("HDR tone mapping constant upload failed.");
            }
            context.commandList.SetPipeline(*pipeline_.pipeline);
            context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *constants.buffer, constants.offset, constants.size);
            context.commandList.SetTexture(rhi::RhiShaderStage::Fragment, 0, source);
            context.commandList.SetSampler(rhi::RhiShaderStage::Fragment, 0, *sampler_);
            context.commandList.Draw(3, 0);
            context.frameData.AdoptTransientRhiObject(std::shared_ptr<rhi::RhiObject>(std::move(sampler_)));
        }

    private:
        struct PipelineState
        {
            rhi::RhiGraphicsPipelineState* pipeline = nullptr;
            rhi::RhiFormat colorFormat = rhi::RhiFormat::Unknown;
            std::weak_ptr<RTShaderResource> shader;
        };

        void EnsurePipeline(RenderPassContext& context)
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

        PipelineState pipeline_;
        std::unique_ptr<rhi::RhiSampler> sampler_;
    };

    HdrToneMappingPass::HdrToneMappingPass(HdrSettings settings)
        : drawResources_(std::make_unique<HdrToneMappingDrawResources>())
        , settings_(SanitizeHdrSettings(settings))
    {
    }

    HdrToneMappingPass::~HdrToneMappingPass() = default;

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
                drawResources_->Draw(context, *source.texture, settings_);
            });
    }

    HdrViewToneMappingPass::HdrViewToneMappingPass(HdrViewToneMappingPassInitParam initParam)
        : drawResources_(std::make_unique<HdrToneMappingDrawResources>())
        , destination_(std::move(initParam.destination))
        , settings_(SanitizeHdrSettings(initParam.settings))
    {
    }

    HdrViewToneMappingPass::~HdrViewToneMappingPass() = default;

    void HdrViewToneMappingPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex)
    {
        if (viewIndex >= graphData.views.size() || !graphData.views[viewIndex].color.IsValid())
        {
            FailToneMapping("HDR view tone mapping requires a valid renderer view color.");
        }
        if (destination_ == nullptr || destination_->GetTexture() == nullptr)
        {
            FailToneMapping("HDR view tone mapping requires an initialized destination texture.");
        }

        rhi::RhiTexture* destinationTexture = destination_->GetTexture();
        if (destinationTexture->GetFormat() != rhi::RhiFormat::Bgra8Unorm && destinationTexture->GetFormat() != rhi::RhiFormat::Rgba8Unorm)
        {
            FailToneMapping("HDR view tone mapping destination must use an 8-bit display format.");
        }

        const FrameGraphTextureHandle destination = frameGraph.ImportTexture(
            destination_->GetDesc().name, BuildToneMappingDestinationDesc(*destinationTexture), ImportedFrameGraphTexture{destinationTexture, false});
        std::string passName = "HdrViewToneMappingPass[";
        passName += std::to_string(viewIndex);
        passName += "]";
        frameGraph.AddRasterPass<ToneMappingPassData>(
            std::move(passName),
            [&graphData, viewIndex, destination](FrameGraphBuilder& builder, ToneMappingPassData& passData)
            {
                RendererViewFrameGraphData& viewGraphData = graphData.views[viewIndex];
                passData.source = builder.Read(viewGraphData.color);
                passData.destination = builder.WriteColorAttachment(destination, rhi::RhiLoadAction::Clear);
                viewGraphData.color = passData.destination;
            },
            [this](const ToneMappingPassData& passData, const FrameGraphPassResources& resources, RenderPassContext& context)
            {
                const ResolvedFrameGraphTexture source = resources.GetTexture(passData.source);
                const ResolvedFrameGraphTexture destination = resources.GetTexture(passData.destination);
                if (source.texture == nullptr || destination.texture == nullptr || source.isSwapchain || destination.isSwapchain)
                {
                    FailToneMapping("HDR view tone mapping resolved invalid source or destination textures.");
                }
                if (!IsHdrFormat(source.texture->GetFormat()))
                {
                    FailToneMapping("HDR view tone mapping source texture must use a floating-point HDR format.");
                }
                if (source.texture->GetWidth() != destination.texture->GetWidth() || source.texture->GetHeight() != destination.texture->GetHeight())
                {
                    FailToneMapping("HDR view tone mapping source and destination extents must match.");
                }
                context.frameData.RetainInFlightGpuFrameObject(destination_->GetTextureShared());
                if (context.frameData.builtInShaderResources == nullptr || context.frameData.builtInShaderResources->hdrToneMapping == nullptr)
                {
                    return;
                }
                drawResources_->Draw(context, *source.texture, settings_);
            });
    }
} // namespace ve::pbr
