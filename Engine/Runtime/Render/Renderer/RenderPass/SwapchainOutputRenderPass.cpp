#include "Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"

#include <exception>
#include <utility>

namespace ve
{
    namespace
    {
        struct SwapchainOverlayPassData
        {
            std::vector<FrameGraphTextureHandle> sampledViewColors;
            FrameGraphTextureHandle swapchainColor;
        };

        struct ViewColorToSwapchainCopyPassData
        {
            FrameGraphTextureHandle source;
            FrameGraphTextureHandle destination;
        };

        [[noreturn]] void FailSwapchainOutputPassInvariant(const char* message)
        {
            VE_LOG_ERROR("{}", message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message);
            std::terminate();
        }
    } // namespace

    SwapchainOverlayRenderPass::SwapchainOverlayRenderPass(SwapchainOverlayRenderPassInitParam initParam) noexcept
        : initParam_(std::move(initParam))
    {
    }

    void SwapchainOverlayRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        if (!graphData.swapchainColor.IsValid())
        {
            FailSwapchainOutputPassInvariant("Swapchain overlay pass requires an imported swapchain color.");
        }
        for (const UInt32 viewIndex : initParam_.sampledViewIndices)
        {
            if (viewIndex >= graphData.views.size() || !graphData.views[viewIndex].color.IsValid())
            {
                FailSwapchainOutputPassInvariant("Swapchain overlay pass references an invalid sampled view color.");
            }
        }

        frameGraph.AddRasterPass<SwapchainOverlayPassData>(
            "SwapchainOverlayPass",
            [this, &graphData](FrameGraphBuilder& builder, SwapchainOverlayPassData& passData)
            {
                passData.sampledViewColors.reserve(initParam_.sampledViewIndices.size());
                for (const UInt32 viewIndex : initParam_.sampledViewIndices)
                {
                    passData.sampledViewColors.push_back(builder.Read(graphData.views[viewIndex].color));
                }
                passData.swapchainColor = builder.WriteColorAttachment(graphData.swapchainColor, initParam_.colorLoadAction, initParam_.clearColor);
                graphData.swapchainColor = passData.swapchainColor;
            },
            [this](const SwapchainOverlayPassData&, RenderPassContext& context)
            {
                if (initParam_.callback)
                {
                    initParam_.callback(context.commandList);
                }
            });
    }

    ViewColorToSwapchainCopyPass::ViewColorToSwapchainCopyPass(UInt32 viewIndex) noexcept
        : viewIndex_(viewIndex)
    {
    }

    void ViewColorToSwapchainCopyPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        if (viewIndex_ >= graphData.views.size() || !graphData.views[viewIndex_].color.IsValid())
        {
            FailSwapchainOutputPassInvariant("View-color copy pass references an invalid source view color.");
        }
        if (!graphData.swapchainColor.IsValid())
        {
            FailSwapchainOutputPassInvariant("View-color copy pass requires an imported swapchain color.");
        }

        frameGraph.AddComputePass<ViewColorToSwapchainCopyPassData>(
            "ViewColorToSwapchainCopyPass",
            [this, &graphData](FrameGraphBuilder& builder, ViewColorToSwapchainCopyPassData& passData)
            {
                passData.source = builder.ReadCopySource(graphData.views[viewIndex_].color);
                passData.destination = builder.WriteCopyDestination(graphData.swapchainColor);
                graphData.swapchainColor = passData.destination;
            },
            [](const ViewColorToSwapchainCopyPassData& passData, const FrameGraphPassResources& resources, RenderPassContext& context)
            {
                const ResolvedFrameGraphTexture source = resources.GetTexture(passData.source);
                const ResolvedFrameGraphTexture destination = resources.GetTexture(passData.destination);
                if (source.texture == nullptr || source.isSwapchain || destination.texture != nullptr || !destination.isSwapchain)
                {
                    FailSwapchainOutputPassInvariant("View-color copy pass resolved invalid source or swapchain resources.");
                }
                if (!context.commandList.CopyTextureToSwapchain(*source.texture, *context.frameData.mainSwapchain))
                {
                    FailSwapchainOutputPassInvariant("View-color copy pass failed to record the texture-to-swapchain copy.");
                }
            });
    }
} // namespace ve
