#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderTexture.h"
#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.h"
#include "Engine/Runtime/Render/Renderer/StandaloneRenderer.h"

#include <memory>
#include <vector>

namespace ve
{
    class ShaderManager;
    namespace rhi
    {
        class RhiCommandList;
    }

    using EditorOverlayRenderCallback = SwapchainOverlayRenderCallback;

    /// Describes the editor frame flow after editor UI has produced draw data on the Scene Thread.
    struct EditorRenderFramePipelineInitParam
    {
        StandaloneRendererInitParam renderer;
        std::vector<std::shared_ptr<RTRenderTexture>> retainedRenderTextures;
    };

    /// Describes the player frame flow: render the scene to an intermediate color texture, then present it.
    struct PlayerRenderFramePipelineInitParam
    {
        BaseRendererInitParam sceneRenderer;
        std::shared_ptr<RTRenderTexture> sceneColorTexture;
    };

    /// Abstracts how a product surface records one frame.
    ///
    /// Renderers decide how to render an RTScene. Frame pipelines decide how editor/player surfaces combine those
    /// renderers with presentation work such as ImGui overlay rendering or copying the player scene color to the
    /// swapchain.
    class FrameRenderPipeline : public NonCopyable
    {
    public:
        FrameRenderPipeline() = default;
        virtual ~FrameRenderPipeline() = default;

        virtual void RenderFrame(const FrameRenderPipelineData& frameData) = 0;
    };

    class EditorRenderFramePipeline final : public FrameRenderPipeline
    {
    public:
        explicit EditorRenderFramePipeline(EditorRenderFramePipelineInitParam initParam);

        void RenderFrame(const FrameRenderPipelineData& frameData) override;

    private:
        StandaloneRendererInitParam rendererInitParam_;
        std::vector<std::shared_ptr<RTRenderTexture>> retainedRenderTextures_;
    };

    class PlayerRenderFramePipeline final : public FrameRenderPipeline
    {
    public:
        explicit PlayerRenderFramePipeline(PlayerRenderFramePipelineInitParam initParam);

        void RenderFrame(const FrameRenderPipelineData& frameData) override;

    private:
        void EnsureSceneColorTexture(const FrameRenderPipelineData& frameData);

        BaseRendererInitParam sceneRenderer_;
        std::shared_ptr<RTRenderTexture> sceneColorTexture_;
    };
} // namespace ve
