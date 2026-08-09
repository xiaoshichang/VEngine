#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Render/PBR/PbrTypes.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <memory>

namespace ve
{
    class RTRenderTexture;
} // namespace ve

namespace ve::pbr
{
    class HdrToneMappingDrawResources;

    /// Converts the HDR scene color into the swapchain's display-referred color space.
    class HdrToneMappingPass final : public RenderPass
    {
    public:
        explicit HdrToneMappingPass(HdrSettings settings = {});
        ~HdrToneMappingPass() override;

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;
        void SetSettings(HdrSettings settings) noexcept;
        [[nodiscard]] const HdrSettings& GetSettings() const noexcept;

    private:
        std::unique_ptr<HdrToneMappingDrawResources> drawResources_;
        HdrSettings settings_;
    };

    struct HdrViewToneMappingPassInitParam
    {
        std::shared_ptr<RTRenderTexture> destination;
        HdrSettings settings;
    };

    /// Converts one HDR renderer view into an LDR texture that can be sampled by a product UI.
    class HdrViewToneMappingPass final : public ViewRenderPass
    {
    public:
        explicit HdrViewToneMappingPass(HdrViewToneMappingPassInitParam initParam);
        ~HdrViewToneMappingPass() override;

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) override;

    private:
        std::unique_ptr<HdrToneMappingDrawResources> drawResources_;
        std::shared_ptr<RTRenderTexture> destination_;
        HdrSettings settings_;
    };
} // namespace ve::pbr
