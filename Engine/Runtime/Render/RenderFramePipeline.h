#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/BaseRenderer.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <functional>
#include <memory>
#include <vector>

namespace ve
{
    using EditorOverlayRenderCallback = std::function<void()>;

    /// Describes the editor frame flow after editor UI has produced draw data on the Scene Thread.
    struct EditorRenderFramePipelineDesc
    {
        std::vector<std::shared_ptr<BaseRenderer>> sceneRenderers;
        rhi::RhiLoadAction overlayColorLoadAction = rhi::RhiLoadAction::Clear;
        EditorOverlayRenderCallback overlayRenderCallback;
    };

    /// Describes the player frame flow: render the scene to an intermediate color texture, then present it.
    struct PlayerRenderFramePipelineDesc
    {
        std::shared_ptr<BaseRenderer> sceneRenderer;
        std::shared_ptr<RTRenderTexture> sceneColorTexture;
    };

    /// Abstracts how a product surface records one frame.
    ///
    /// Renderers decide how to render an RTScene. Frame pipelines decide how editor/player surfaces combine those
    /// renderers with presentation work such as ImGui overlay rendering or copying the player scene color to the
    /// swapchain.
    class RenderFramePipeline : public NonCopyable
    {
    public:
        RenderFramePipeline() = default;
        virtual ~RenderFramePipeline() = default;

        [[nodiscard]] virtual ErrorCode RenderFrame(rhi::RhiDevice& device, rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain) = 0;
    };

    class EditorRenderFramePipeline final : public RenderFramePipeline
    {
    public:
        explicit EditorRenderFramePipeline(EditorRenderFramePipelineDesc desc);

        [[nodiscard]] ErrorCode RenderFrame(rhi::RhiDevice& device, rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain) override;

    private:
        [[nodiscard]] ErrorCode RecordOverlayPass(rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain);

        std::vector<std::shared_ptr<BaseRenderer>> sceneRenderers_;
        rhi::RhiLoadAction overlayColorLoadAction_ = rhi::RhiLoadAction::Clear;
        EditorOverlayRenderCallback overlayRenderCallback_;
    };

    class PlayerRenderFramePipeline final : public RenderFramePipeline
    {
    public:
        explicit PlayerRenderFramePipeline(PlayerRenderFramePipelineDesc desc);

        [[nodiscard]] ErrorCode RenderFrame(rhi::RhiDevice& device, rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain) override;

    private:
        void EnsureSceneColorTexture(rhi::RhiDevice& device, const rhi::RhiSwapchain& mainSwapchain);
        [[nodiscard]] ErrorCode CopySceneColorToSwapchain(rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain);

        std::shared_ptr<BaseRenderer> sceneRenderer_;
        std::shared_ptr<RTRenderTexture> sceneColorTexture_;
    };
} // namespace ve
