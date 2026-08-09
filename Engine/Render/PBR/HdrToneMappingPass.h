#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Render/PBR/PbrTypes.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <memory>

namespace ve
{
    class RTShaderResource;
}

namespace ve::pbr
{
    /// Converts the HDR scene color into the swapchain's display-referred color space.
    class HdrToneMappingPass final : public RenderPass
    {
    public:
        explicit HdrToneMappingPass(HdrSettings settings = {});

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;
        void SetSettings(HdrSettings settings) noexcept;
        [[nodiscard]] const HdrSettings& GetSettings() const noexcept;

    private:
        struct PipelineState
        {
            rhi::RhiGraphicsPipelineState* pipeline = nullptr;
            rhi::RhiFormat colorFormat = rhi::RhiFormat::Unknown;
            std::weak_ptr<RTShaderResource> shader;
        };

        void EnsurePipeline(RenderPassContext& context);

        PipelineState pipeline_;
        std::unique_ptr<rhi::RhiSampler> sampler_;
        HdrSettings settings_;
    };
} // namespace ve::pbr
