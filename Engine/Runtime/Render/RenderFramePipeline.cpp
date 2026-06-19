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

    EditorRenderFramePipeline::EditorRenderFramePipeline(EditorRenderFramePipelineDesc desc)
        : sceneRenderers_(std::move(desc.sceneRenderers))
        , overlayColorLoadAction_(desc.overlayColorLoadAction)
        , overlayRenderCallback_(std::move(desc.overlayRenderCallback))
    {
    }

    ErrorCode EditorRenderFramePipeline::RenderFrame(rhi::RhiDevice& device, rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain)
    {
        VE_ASSERT_RENDER_THREAD();

        if (!commandList.Begin())
        {
            return ErrorCode::PlatformError;
        }

        for (const std::shared_ptr<BaseRenderer>& renderer : sceneRenderers_)
        {
            if (renderer == nullptr)
            {
                continue;
            }

            const ErrorCode rendererResult = renderer->RenderScene(device, commandList, mainSwapchain);
            if (rendererResult != ErrorCode::None)
            {
                const bool ended = commandList.End();
                VE_ASSERT_MESSAGE(ended, "EditorRenderFramePipeline failed to end command list after renderer failure.");
                return rendererResult;
            }
        }

        const ErrorCode overlayResult = RecordOverlayPass(commandList, mainSwapchain);
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

    ErrorCode EditorRenderFramePipeline::RecordOverlayPass(rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain)
    {
        const rhi::RhiRenderPassDesc passDesc = BuildOverlayRenderPassDesc(mainSwapchain, overlayColorLoadAction_);
        if (!commandList.BeginRenderPass(mainSwapchain, passDesc))
        {
            return ErrorCode::PlatformError;
        }

        commandList.SetViewport(BuildMainViewport(mainSwapchain));
        commandList.SetScissor(BuildMainScissor(mainSwapchain));

        if (overlayRenderCallback_)
        {
            overlayRenderCallback_();
        }

        commandList.EndRenderPass();
        return ErrorCode::None;
    }

    PlayerRenderFramePipeline::PlayerRenderFramePipeline(PlayerRenderFramePipelineDesc desc)
        : sceneRenderer_(std::move(desc.sceneRenderer))
        , sceneColorTexture_(std::move(desc.sceneColorTexture))
    {
    }

    ErrorCode PlayerRenderFramePipeline::RenderFrame(rhi::RhiDevice& device, rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain)
    {
        VE_ASSERT_RENDER_THREAD();

        if (sceneRenderer_ == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        EnsureSceneColorTexture(device, mainSwapchain);

        if (!commandList.Begin())
        {
            return ErrorCode::PlatformError;
        }

        const ErrorCode rendererResult = sceneRenderer_->RenderScene(device, commandList, mainSwapchain);
        if (rendererResult != ErrorCode::None)
        {
            const bool ended = commandList.End();
            VE_ASSERT_MESSAGE(ended, "PlayerRenderFramePipeline failed to end command list after renderer failure.");
            return rendererResult;
        }

        const ErrorCode copyResult = CopySceneColorToSwapchain(commandList, mainSwapchain);
        if (copyResult != ErrorCode::None)
        {
            const bool ended = commandList.End();
            VE_ASSERT_MESSAGE(ended, "PlayerRenderFramePipeline failed to end command list after copy failure.");
            return copyResult;
        }

        if (!commandList.End())
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
