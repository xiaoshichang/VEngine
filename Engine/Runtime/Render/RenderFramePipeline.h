#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/BaseRenderer.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <functional>
#include <memory>
#include <vector>

namespace ve
{
    class ShaderManager;

    using EditorOverlayRenderCallback = std::function<void()>;

    /// Describes the editor frame flow after editor UI has produced draw data on the Scene Thread.
    struct EditorRenderFramePipelineInitParam
    {
        std::vector<ForwardRendererInitParam> sceneRenderers;
        rhi::RhiLoadAction overlayColorLoadAction = rhi::RhiLoadAction::Clear;
        EditorOverlayRenderCallback overlayRenderCallback;
    };

    /// Describes the player frame flow: render the scene to an intermediate color texture, then present it.
    struct PlayerRenderFramePipelineInitParam
    {
        ForwardRendererInitParam sceneRenderer;
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

        [[nodiscard]] virtual ErrorCode RenderFrame(const FrameRenderPipelineData& frameData) = 0;
    };

    class EditorRenderFramePipeline final : public FrameRenderPipeline
    {
    public:
        explicit EditorRenderFramePipeline(EditorRenderFramePipelineInitParam initParam);

        [[nodiscard]] ErrorCode RenderFrame(const FrameRenderPipelineData& frameData) override;

    private:
        [[nodiscard]] ErrorCode RecordOverlayPass(const FrameRenderPipelineData& frameData);

        std::vector<ForwardRendererInitParam> sceneRenderers_;
        rhi::RhiLoadAction overlayColorLoadAction_ = rhi::RhiLoadAction::Clear;
        EditorOverlayRenderCallback overlayRenderCallback_;
    };

    class PlayerRenderFramePipeline final : public FrameRenderPipeline
    {
    public:
        explicit PlayerRenderFramePipeline(PlayerRenderFramePipelineInitParam initParam);

        [[nodiscard]] ErrorCode RenderFrame(const FrameRenderPipelineData& frameData) override;

    private:
        void EnsureSceneColorTexture(rhi::RhiDevice& device, const rhi::RhiSwapchain& mainSwapchain);
        [[nodiscard]] ErrorCode CopySceneColorToSwapchain(rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain);

        ForwardRendererInitParam sceneRenderer_;
        std::shared_ptr<RTRenderTexture> sceneColorTexture_;
    };
} // namespace ve
