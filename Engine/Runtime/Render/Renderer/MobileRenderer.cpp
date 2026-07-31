#include "Engine/Runtime/Render/Renderer/MobileRenderer.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h"

#include <exception>
#include <limits>
#include <utility>

namespace ve
{
    MobileRenderer::MobileRenderer(MobileRendererInitParam initParam)
        : BaseRenderer(std::move(static_cast<BaseRendererInitParam&>(initParam)))
        , transparentPass_(TransparentSceneRenderPassInitParam{})
    {
    }

    void MobileRenderer::BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        if (graphData.views.size() > static_cast<SizeT>(std::numeric_limits<UInt32>::max()))
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Mobile renderer view count is outside its per-view pass range.");
            std::terminate();
        }

        if (opaquePasses_.size() != graphData.views.size() || !hasDepthPrePass_)
        {
            opaquePasses_.clear();
            opaquePasses_.reserve(graphData.views.size());
            for (SizeT viewIndex = 0; viewIndex < graphData.views.size(); ++viewIndex)
            {
                opaquePasses_.push_back(std::make_unique<OpaqueSceneRenderPass>(OpaqueSceneRenderPassInitParam{false, true}));
            }
            hasDepthPrePass_ = true;
        }

        for (SizeT viewIndex = 0; viewIndex < graphData.views.size(); ++viewIndex)
        {
            depthPrePass_.AddToFrameGraph(frameGraph, graphData, static_cast<UInt32>(viewIndex));
        }
        VirtualShadowManager* virtualShadowManager = GetFrameData().virtualShadowManager;
        if (virtualShadowManager == nullptr)
        {
            FailVirtualShadow("Mobile renderer requires the RenderSystem virtual-shadow manager.");
        }
        virtualShadowManager->AddToFrameGraph(GetFrameData(), frameGraph, GetMutableRendererData(), graphData);
        for (SizeT viewIndex = 0; viewIndex < graphData.views.size(); ++viewIndex)
        {
            const UInt32 passViewIndex = static_cast<UInt32>(viewIndex);
            opaquePasses_[viewIndex]->AddToFrameGraph(frameGraph, graphData, passViewIndex);
            transparentPass_.AddToFrameGraph(frameGraph, graphData, passViewIndex);
        }
    }
} // namespace ve
