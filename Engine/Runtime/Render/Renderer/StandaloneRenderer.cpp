#include "Engine/Runtime/Render/Renderer/StandaloneRenderer.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h"

#include <exception>
#include <limits>
#include <utility>

namespace ve
{
    namespace
    {
        [[noreturn]] void FailStandaloneRenderer(const char* message)
        {
            VE_LOG_ERROR("StandaloneRenderer: {}", message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message);
            std::terminate();
        }
    } // namespace

    void ResolveStandaloneViewVirtualShadowVisualization(SizeT viewCount,
                                                         bool fallbackVisualization,
                                                         const std::vector<bool>& configuredVisualization,
                                                         std::vector<bool>& resolvedVisualization)
    {
        if (!configuredVisualization.empty() && configuredVisualization.size() != viewCount)
        {
            FailStandaloneRenderer("per-view VSM visualization count must match the render-view count.");
        }

        if (configuredVisualization.empty())
        {
            resolvedVisualization.assign(viewCount, fallbackVisualization);
        }
        else
        {
            resolvedVisualization = configuredVisualization;
        }
    }

    void AppendStandaloneRendererViewConfiguration(StandaloneRendererInitParam& familyRenderer,
                                                   StandaloneRendererInitParam& sourceRenderer,
                                                   UInt32 familyViewIndex)
    {
        if (!sourceRenderer.viewVisualizeVirtualShadowPages.empty() && sourceRenderer.viewVisualizeVirtualShadowPages.size() != 1)
        {
            FailStandaloneRenderer("appended view configuration must contain at most one VSM visualization setting.");
        }
        for (const RendererViewPassExtension& extension : sourceRenderer.viewExtensions)
        {
            if (extension.viewIndex != 0)
            {
                FailStandaloneRenderer("appended view extensions must reference their source renderer's only view.");
            }
        }

        const bool visualizeVirtualShadowPages = sourceRenderer.viewVisualizeVirtualShadowPages.empty()
                                                     ? sourceRenderer.visualizeVirtualShadowPages
                                                     : sourceRenderer.viewVisualizeVirtualShadowPages.front();
        familyRenderer.viewVisualizeVirtualShadowPages.push_back(visualizeVirtualShadowPages);
        for (std::unique_ptr<RenderPass>& outputPass : sourceRenderer.outputPasses)
        {
            familyRenderer.outputPasses.push_back(std::move(outputPass));
        }
        for (RendererViewPassExtension& extension : sourceRenderer.viewExtensions)
        {
            extension.viewIndex = familyViewIndex;
            familyRenderer.viewExtensions.push_back(std::move(extension));
        }
    }

    StandaloneRenderer::StandaloneRenderer(StandaloneRendererInitParam initParam)
        : BaseRenderer(std::move(static_cast<BaseRendererInitParam&>(initParam)))
        , transparentPass_(TransparentSceneRenderPassInitParam{})
        , viewExtensions_(std::move(initParam.viewExtensions))
    {
        std::vector<bool> viewVisualization;
        ResolveStandaloneViewVirtualShadowVisualization(
            GetRendererData().views.size(), initParam.visualizeVirtualShadowPages, initParam.viewVisualizeVirtualShadowPages, viewVisualization);

        opaquePasses_.reserve(viewVisualization.size());
        for (const bool visualizeVirtualShadowPages : viewVisualization)
        {
            opaquePasses_.push_back(std::make_unique<OpaqueSceneRenderPass>(OpaqueSceneRenderPassInitParam{visualizeVirtualShadowPages, true}));
        }
    }

    void StandaloneRenderer::BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        AddSceneAndExtensionPasses(frameGraph, graphData);
    }

    void StandaloneRenderer::AddSceneAndExtensionPasses(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        if (graphData.views.size() > static_cast<SizeT>(std::numeric_limits<UInt32>::max()))
        {
            FailStandaloneRenderer("view count exceeds its pass-index range.");
        }
        if (opaquePasses_.size() != graphData.views.size())
        {
            FailStandaloneRenderer("opaque-pass count must match its render-view count.");
        }

        for (const RendererViewPassExtension& extension : viewExtensions_)
        {
            if (extension.viewIndex >= graphData.views.size())
            {
                FailStandaloneRenderer("view extension references an out-of-bounds view.");
            }
            for (const std::unique_ptr<ViewRenderPass>& pass : extension.passes)
            {
                if (pass == nullptr)
                {
                    FailStandaloneRenderer("view extension contains a null pass.");
                }
            }
        }

        // Product UI may render an output-only frame before a project scene exists.
        if (graphData.views.empty())
        {
            return;
        }

        for (SizeT viewIndex = 0; viewIndex < graphData.views.size(); ++viewIndex)
        {
            depthPrePass_.AddToFrameGraph(frameGraph, graphData, static_cast<UInt32>(viewIndex));
        }
        VirtualShadowManager* virtualShadowManager = GetFrameData().virtualShadowManager;
        if (virtualShadowManager == nullptr)
        {
            FailStandaloneRenderer("requires the RenderSystem virtual-shadow manager.");
        }
        virtualShadowManager->AddToFrameGraph(GetFrameData(), frameGraph, GetMutableRendererData(), graphData);
        for (SizeT viewIndex = 0; viewIndex < graphData.views.size(); ++viewIndex)
        {
            const UInt32 passViewIndex = static_cast<UInt32>(viewIndex);
            opaquePasses_[viewIndex]->AddToFrameGraph(frameGraph, graphData, passViewIndex);
            transparentPass_.AddToFrameGraph(frameGraph, graphData, passViewIndex);
        }
        for (RendererViewPassExtension& extension : viewExtensions_)
        {
            for (std::unique_ptr<ViewRenderPass>& pass : extension.passes)
            {
                pass->AddToFrameGraph(frameGraph, graphData, extension.viewIndex);
            }
        }
    }
} // namespace ve
