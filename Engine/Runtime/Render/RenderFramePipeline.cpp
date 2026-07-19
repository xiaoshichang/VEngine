#include "Engine/Runtime/Render/RenderFramePipeline.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RendererFactory.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{
    namespace
    {
        struct SwapchainRasterPassData
        {
            FrameGraphTextureHandle color;
        };

        [[nodiscard]] ErrorCode RecordSwapchainRasterPass(const FrameRenderPipelineData& frameData,
                                                          const char* passName,
                                                          rhi::RhiLoadAction loadAction,
                                                          rhi::RhiColor clearColor,
                                                          const EditorOverlayRenderCallback& renderCallback)
        {
            VE_ASSERT(frameData.mainSwapchain != nullptr);
            const rhi::RhiExtent2D extent = frameData.mainSwapchain->GetExtent();
            RendererData rendererData = {};
            FrameGraph frameGraph(FrameGraphExecuteContext{frameData, rendererData});
            FrameGraphTextureHandle color;

            const ErrorCode setupResult = frameGraph.Setup(
                [&](FrameGraph& setupGraph)
                {
                    FrameGraphTextureDesc colorDesc = {};
                    colorDesc.width = extent.width;
                    colorDesc.height = extent.height;
                    colorDesc.format = frameData.mainSwapchain->GetColorFormat();
                    colorDesc.usage = rhi::RhiTextureUsage::RenderTarget;
                    color = setupGraph.ImportTexture("MainSwapchainColor", colorDesc, ImportedFrameGraphTexture{nullptr, true});

                    setupGraph.AddRasterPass<SwapchainRasterPassData>(
                        passName,
                        [&](FrameGraphBuilder& builder, SwapchainRasterPassData& passData)
                        {
                            passData.color = builder.WriteColorAttachment(color, loadAction, clearColor);
                            color = passData.color;
                        },
                        [&renderCallback](const SwapchainRasterPassData&, RenderPassContext& context)
                        {
                            if (renderCallback)
                            {
                                renderCallback(context.commandList);
                            }
                            return ErrorCode::None;
                        });
                    setupGraph.Export(color);
                    return ErrorCode::None;
                });
            if (setupResult != ErrorCode::None)
            {
                return setupResult;
            }

            const Error compileResult = frameGraph.Compile();
            if (!compileResult.IsOk())
            {
                VE_LOG_ERROR("Frame graph compile failed for %s: %s", passName, compileResult.GetMessage().c_str());
                return compileResult.GetCode();
            }
            return frameGraph.Execute();
        }

        [[nodiscard]] RenderTextureDesc BuildSceneColorTextureDesc(const rhi::RhiSwapchain& mainSwapchain)
        {
            const rhi::RhiExtent2D extent = mainSwapchain.GetExtent();

            RenderTextureDesc desc = {};
            desc.name = "PlayerSceneColor";
            desc.extent = WindowExtent{extent.width, extent.height};
            desc.colorFormat = mainSwapchain.GetColorFormat();
            return desc;
        }
    } // namespace

    EditorRenderFramePipeline::EditorRenderFramePipeline(EditorRenderFramePipelineInitParam initParam)
        : sceneRenderers_(std::move(initParam.sceneRenderers))
        , overlayColorLoadAction_(initParam.overlayColorLoadAction)
        , overlayRenderCallback_(std::move(initParam.overlayRenderCallback))
    {
    }

    ErrorCode EditorRenderFramePipeline::RenderFrame(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_RENDER_THREAD();
        rhi::RhiCommandList& commandList = frameData.GetCommandList();

        if (!commandList.Begin())
        {
            return ErrorCode::PlatformError;
        }

        for (StandaloneRendererInitParam& rendererInitParam : sceneRenderers_)
        {
            rendererInitParam.frameData = &frameData;
            StandaloneRenderer renderer(std::move(rendererInitParam));
            const ErrorCode renderResult = renderer.RenderScene();
            if (renderResult != ErrorCode::None)
            {
                const bool ended = commandList.End();
                VE_ASSERT_MESSAGE(ended, "EditorRenderFramePipeline failed to end command list after renderer failure.");
                return renderResult;
            }
        }

        const ErrorCode overlayResult = RecordOverlayPass(frameData);
        if (overlayResult != ErrorCode::None)
        {
            const bool ended = commandList.End();
            VE_ASSERT_MESSAGE(ended, "EditorRenderFramePipeline failed to end command list after overlay failure.");
            return overlayResult;
        }

        if (!commandList.End())
        {
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorRenderFramePipeline::RecordOverlayPass(const FrameRenderPipelineData& frameData)
    {
        return RecordSwapchainRasterPass(
            frameData, "EditorOverlayPass", overlayColorLoadAction_, rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f}, overlayRenderCallback_);
    }

    PlayerRenderFramePipeline::PlayerRenderFramePipeline(PlayerRenderFramePipelineInitParam initParam)
        : sceneRenderer_(std::move(initParam.sceneRenderer))
        , sceneColorTexture_(std::move(initParam.sceneColorTexture))
    {
    }

    ErrorCode PlayerRenderFramePipeline::RenderFrame(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(frameData.device != nullptr);
        VE_ASSERT(frameData.mainSwapchain != nullptr);
        rhi::RhiCommandList& commandList = frameData.GetCommandList();

        EnsureSceneColorTexture(frameData);

        if (!commandList.Begin())
        {
            return ErrorCode::PlatformError;
        }

        if (sceneRenderer_.scene != nullptr)
        {
            sceneRenderer_.frameData = &frameData;
            std::unique_ptr<BaseRenderer> sceneRenderer = CreatePlayerRenderer(std::move(sceneRenderer_));
            if (sceneRenderer == nullptr)
            {
                const bool ended = commandList.End();
                VE_ASSERT_MESSAGE(ended, "PlayerRenderFramePipeline failed to end command list after renderer creation failure.");
                return ErrorCode::InvalidState;
            }

            const ErrorCode renderResult = sceneRenderer->RenderScene();
            if (renderResult != ErrorCode::None)
            {
                const bool ended = commandList.End();
                VE_ASSERT_MESSAGE(ended, "PlayerRenderFramePipeline failed to end command list after renderer failure.");
                return renderResult;
            }

            const ErrorCode copyResult = CopySceneColorToSwapchain(commandList, *frameData.mainSwapchain);
            if (copyResult != ErrorCode::None)
            {
                const bool ended = commandList.End();
                VE_ASSERT_MESSAGE(ended, "PlayerRenderFramePipeline failed to end command list after copy failure.");
                return copyResult;
            }
        }
        else
        {
            const ErrorCode emptyFrameResult =
                RecordSwapchainRasterPass(frameData, "EmptyPlayerFramePass", rhi::RhiLoadAction::Clear, rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f}, {});
            if (emptyFrameResult != ErrorCode::None)
            {
                const bool ended = commandList.End();
                VE_ASSERT_MESSAGE(ended, "PlayerRenderFramePipeline failed to end command list after empty frame failure.");
                return emptyFrameResult;
            }
        }

        if (!commandList.End())
        {
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    void PlayerRenderFramePipeline::EnsureSceneColorTexture(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_RENDER_THREAD();
        if (sceneColorTexture_ == nullptr)
        {
            return;
        }

        VE_ASSERT(frameData.device != nullptr);
        VE_ASSERT(frameData.mainSwapchain != nullptr);
        std::vector<std::unique_ptr<rhi::RhiObject>> retiredResources;
        RenderTextureDesc desc = BuildSceneColorTextureDesc(*frameData.mainSwapchain);
        desc.optimizedClearColor = sceneColorTexture_->GetDesc().optimizedClearColor;
        sceneColorTexture_->InitRenderResource(*frameData.device, std::move(desc), retiredResources);
        for (std::unique_ptr<rhi::RhiObject>& resource : retiredResources)
        {
            frameData.RetainTransientResource(std::move(resource));
        }
    }

    ErrorCode PlayerRenderFramePipeline::CopySceneColorToSwapchain(rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain)
    {
        if (sceneColorTexture_ == nullptr || sceneColorTexture_->GetTexture() == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        if (!commandList.CopyTextureToSwapchain(*sceneColorTexture_->GetTexture(), mainSwapchain))
        {
            return ErrorCode::Unsupported;
        }

        return ErrorCode::None;
    }
} // namespace ve
