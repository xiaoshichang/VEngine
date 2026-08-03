#pragma once

#include "Engine/Runtime/Render/RenderDebugMode.h"
#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/ShadowCasterDirtyDebugPass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowRedrawPageDebugPass.h"

#include <memory>
#include <vector>

namespace ve
{
    struct StandaloneRendererInitParam : public BaseRendererInitParam
    {
        RenderDebugMode debugMode = RenderDebugMode::None;
        std::vector<RenderDebugMode> viewDebugModes;
        std::vector<RendererViewPassExtension> viewExtensions;
    };

    /// Resolves an optional explicit per-view setting, falling back to the family-wide debug mode.
    void ResolveStandaloneViewDebugModes(SizeT viewCount,
                                         RenderDebugMode fallbackMode,
                                         const std::vector<RenderDebugMode>& configuredModes,
                                         std::vector<RenderDebugMode>& resolvedModes);
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
        VirtualShadowRedrawPageDebugPass vsmRedrawPagePass_;
        ShadowCasterDirtyDebugPass shadowCasterDirtyPass_;
        std::vector<RenderDebugMode> debugModes_;
        std::vector<RendererViewPassExtension> viewExtensions_;
    };
} // namespace ve
