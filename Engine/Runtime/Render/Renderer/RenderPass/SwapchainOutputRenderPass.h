#pragma once

#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <functional>
#include <vector>

namespace ve
{
    using SwapchainOverlayRenderCallback = std::function<void(rhi::RhiCommandList& commandList)>;

    struct SwapchainOverlayRenderPassInitParam
    {
        std::vector<UInt32> sampledViewIndices;
        rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
        rhi::RhiColor clearColor = {};
        SwapchainOverlayRenderCallback callback;
    };

    /// Declares final view colors as shader inputs before drawing a product overlay into the current swapchain color version.
    class SwapchainOverlayRenderPass final : public RenderPass
    {
    public:
        explicit SwapchainOverlayRenderPass(SwapchainOverlayRenderPassInitParam initParam) noexcept;

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        SwapchainOverlayRenderPassInitParam initParam_;
    };

    /// Copies one final offscreen view color into the swapchain after all scene and extension passes have completed.
    class ViewColorToSwapchainCopyPass final : public RenderPass
    {
    public:
        explicit ViewColorToSwapchainCopyPass(UInt32 viewIndex) noexcept;

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        UInt32 viewIndex_ = 0;
    };
} // namespace ve
