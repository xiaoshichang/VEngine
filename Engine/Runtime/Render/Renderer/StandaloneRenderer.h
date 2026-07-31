#pragma once

#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h"

#include <memory>
#include <vector>

namespace ve
{
    struct StandaloneRendererInitParam : public BaseRendererInitParam
    {
        bool visualizeVirtualShadowPages = false;
        std::vector<bool> viewVisualizeVirtualShadowPages;
        std::vector<RendererViewPassExtension> viewExtensions;
    };

    /// Resolves an optional explicit per-view setting, falling back to the transitional family-wide setting.
    void ResolveStandaloneViewVirtualShadowVisualization(SizeT viewCount,
                                                         bool fallbackVisualization,
                                                         const std::vector<bool>& configuredVisualization,
                                                         std::vector<bool>& resolvedVisualization);
    void
    AppendStandaloneRendererViewConfiguration(StandaloneRendererInitParam& familyRenderer, StandaloneRendererInitParam& sourceRenderer, UInt32 familyViewIndex);

    /// Host-platform renderer with scene passes followed by optional editor passes.
    class StandaloneRenderer final : public BaseRenderer
    {
    public:
        explicit StandaloneRenderer(StandaloneRendererInitParam initParam);

    private:
        void BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;
        void AddSceneAndExtensionPasses(FrameGraph& frameGraph, RendererFrameGraphData& graphData);

        DepthPrePass depthPrePass_;
        std::vector<std::unique_ptr<OpaqueSceneRenderPass>> opaquePasses_;
        TransparentSceneRenderPass transparentPass_;
        std::vector<RendererViewPassExtension> viewExtensions_;
    };
} // namespace ve
