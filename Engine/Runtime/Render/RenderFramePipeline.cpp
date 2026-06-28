#include "Engine/Runtime/Render/RenderFramePipeline.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiRenderPassDesc BuildOverlayRenderPassDesc(rhi::RhiSwapchain& mainSwapchain, rhi::RhiLoadAction colorLoadAction) noexcept
        {
            rhi::RhiRenderPassDesc desc = {};
            desc.debugName = "EditorOverlayPass";
            const rhi::RhiExtent2D extent = mainSwapchain.GetExtent();
            desc.renderArea = rhi::RhiRenderArea{0, 0, extent.width, extent.height};
            desc.colorAttachmentCount = 1;
            desc.colorAttachments[0].texture = nullptr;
            desc.colorAttachments[0].loadAction = colorLoadAction;
            desc.colorAttachments[0].storeAction = rhi::RhiStoreAction::Store;
            desc.colorAttachments[0].clearColor = rhi::RhiColor{0.05f, 0.07f, 0.10f, 1.0f};
            return desc;
        }

        [[nodiscard]] rhi::RhiViewport BuildMainViewport(rhi::RhiSwapchain& mainSwapchain) noexcept
        {
            const rhi::RhiExtent2D extent = mainSwapchain.GetExtent();
            return rhi::RhiViewport{0.0f, 0.0f, static_cast<Float32>(extent.width), static_cast<Float32>(extent.height), 0.0f, 1.0f};
        }

        [[nodiscard]] rhi::RhiScissorRect BuildMainScissor(rhi::RhiSwapchain& mainSwapchain) noexcept
        {
            const rhi::RhiExtent2D extent = mainSwapchain.GetExtent();
            return rhi::RhiScissorRect{0, 0, extent.width, extent.height};
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
        VE_ASSERT(frameData.commandList != nullptr);

        if (!frameData.commandList->Begin())
        {
            return ErrorCode::PlatformError;
        }

        for (ForwardRendererInitParam& rendererInitParam : sceneRenderers_)
        {
            rendererInitParam.frameData = &frameData;
            ForwardRenderer renderer(std::move(rendererInitParam));
            renderer.RenderScene();
        }

        const ErrorCode overlayResult = RecordOverlayPass(frameData);
        if (overlayResult != ErrorCode::None)
        {
            const bool ended = frameData.commandList->End();
            VE_ASSERT_MESSAGE(ended, "EditorRenderFramePipeline failed to end command list after overlay failure.");
            return overlayResult;
        }

        if (!frameData.commandList->End())
        {
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorRenderFramePipeline::RecordOverlayPass(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT(frameData.commandList != nullptr);
        VE_ASSERT(frameData.mainSwapchain != nullptr);

        const rhi::RhiRenderPassDesc passDesc = BuildOverlayRenderPassDesc(*frameData.mainSwapchain, overlayColorLoadAction_);
        if (!frameData.commandList->BeginRenderPass(*frameData.mainSwapchain, passDesc))
        {
            return ErrorCode::PlatformError;
        }

        frameData.commandList->SetViewport(BuildMainViewport(*frameData.mainSwapchain));
        frameData.commandList->SetScissor(BuildMainScissor(*frameData.mainSwapchain));

        if (overlayRenderCallback_)
        {
            overlayRenderCallback_(*frameData.commandList);
        }

        frameData.commandList->EndRenderPass();
        return ErrorCode::None;
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
        VE_ASSERT(frameData.commandList != nullptr);
        VE_ASSERT(frameData.mainSwapchain != nullptr);

        EnsureSceneColorTexture(*frameData.device, *frameData.mainSwapchain);

        if (!frameData.commandList->Begin())
        {
            return ErrorCode::PlatformError;
        }

        sceneRenderer_.frameData = &frameData;
        ForwardRenderer sceneRenderer(std::move(sceneRenderer_));
        sceneRenderer.RenderScene();

        const ErrorCode copyResult = CopySceneColorToSwapchain(*frameData.commandList, *frameData.mainSwapchain);
        if (copyResult != ErrorCode::None)
        {
            const bool ended = frameData.commandList->End();
            VE_ASSERT_MESSAGE(ended, "PlayerRenderFramePipeline failed to end command list after copy failure.");
            return copyResult;
        }

        if (!frameData.commandList->End())
        {
            return ErrorCode::PlatformError;
        }

        return ErrorCode::None;
    }

    void PlayerRenderFramePipeline::EnsureSceneColorTexture(rhi::RhiDevice& device, const rhi::RhiSwapchain& mainSwapchain)
    {
        VE_ASSERT_RENDER_THREAD();
        if (sceneColorTexture_ == nullptr)
        {
            return;
        }

        sceneColorTexture_->InitRenderResource(device, BuildSceneColorTextureDesc(mainSwapchain));
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
