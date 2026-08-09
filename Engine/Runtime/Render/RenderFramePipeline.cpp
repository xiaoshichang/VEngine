#include "Engine/Runtime/Render/RenderFramePipeline.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.h"
#include "Engine/Runtime/Render/Renderer/RendererFactory.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <exception>
#include <memory>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] RenderTextureDesc BuildSceneColorTextureDesc(const rhi::RhiSwapchain& mainSwapchain)
        {
            const rhi::RhiExtent2D extent = mainSwapchain.GetExtent();

            RenderTextureDesc desc = {};
            desc.name = "PlayerSceneColor";
            desc.extent = WindowExtent{extent.width, extent.height};
            desc.colorFormat = mainSwapchain.GetColorFormat();
            return desc;
        }

        [[noreturn]] void FailFramePipeline(const char* message)
        {
            VE_LOG_ERROR("{}", message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message);
            std::terminate();
        }

    } // namespace

    EditorRenderFramePipeline::EditorRenderFramePipeline(EditorRenderFramePipelineInitParam initParam)
        : rendererInitParam_(std::move(initParam.renderer))
        , retainedRenderTextures_(std::move(initParam.retainedRenderTextures))
        , builtInShaderResources_(std::move(initParam.builtInShaderResources))
    {
    }

    const BuiltInShaderResources* EditorRenderFramePipeline::GetBuiltInShaderResources() const noexcept
    {
        return builtInShaderResources_.get();
    }

    void EditorRenderFramePipeline::RenderFrame(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_RENDER_THREAD();
        rhi::RhiCommandList& commandList = frameData.GetCommandList();

        if (!commandList.Begin())
        {
            FailFramePipeline("Editor frame pipeline failed to begin its command list.");
        }

        rendererInitParam_.frameData = &frameData;
        StandaloneRenderer renderer(std::move(rendererInitParam_));
        renderer.Render();

        if (!commandList.End())
        {
            FailFramePipeline("Editor frame pipeline failed to end its command list.");
        }
    }

    PlayerRenderFramePipeline::PlayerRenderFramePipeline(PlayerRenderFramePipelineInitParam initParam)
        : sceneRenderer_(std::move(initParam.sceneRenderer))
        , sceneColorTexture_(std::move(initParam.sceneColorTexture))
        , builtInShaderResources_(std::move(initParam.builtInShaderResources))
    {
        sceneRenderer_.outputPasses.push_back(std::make_unique<ViewColorToSwapchainCopyPass>(0));
    }

    const BuiltInShaderResources* PlayerRenderFramePipeline::GetBuiltInShaderResources() const noexcept
    {
        return builtInShaderResources_.get();
    }

    void PlayerRenderFramePipeline::RenderFrame(const FrameRenderPipelineData& frameData)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(frameData.device != nullptr);
        VE_ASSERT(frameData.mainSwapchain != nullptr);
        rhi::RhiCommandList& commandList = frameData.GetCommandList();

        EnsureSceneColorTexture(frameData);

        if (!commandList.Begin())
        {
            FailFramePipeline("Player frame pipeline failed to begin its command list.");
        }

        sceneRenderer_.frameData = &frameData;
        std::unique_ptr<BaseRenderer> sceneRenderer = CreatePlayerRenderer(std::move(sceneRenderer_));
        if (sceneRenderer == nullptr)
        {
            FailFramePipeline("Player frame pipeline failed to create its renderer.");
        }

        sceneRenderer->Render();

        if (!commandList.End())
        {
            FailFramePipeline("Player frame pipeline failed to end its command list.");
        }
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
        RenderTextureDesc desc = BuildSceneColorTextureDesc(*frameData.mainSwapchain);
        sceneColorTexture_->InitRenderResource(*frameData.device, std::move(desc));
    }

} // namespace ve
