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
    struct RenderShaderResources;
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
        std::shared_ptr<const RenderShaderResources> shaderResources;
    };

    /// Describes the player frame flow: render the scene to an intermediate color texture, then present it.
    struct PlayerRenderFramePipelineInitParam
    {
        BaseRendererInitParam sceneRenderer;
        std::shared_ptr<RTRenderTexture> sceneColorTexture;
        std::shared_ptr<const RenderShaderResources> shaderResources;
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
        [[nodiscard]] virtual const RenderShaderResources* GetShaderResources() const noexcept = 0;
    };

    class EditorRenderFramePipeline final : public FrameRenderPipeline
    {
    public:
        explicit EditorRenderFramePipeline(EditorRenderFramePipelineInitParam initParam);

        void RenderFrame(const FrameRenderPipelineData& frameData) override;
        [[nodiscard]] const RenderShaderResources* GetShaderResources() const noexcept override;

    private:
        StandaloneRendererInitParam rendererInitParam_;
        std::vector<std::shared_ptr<RTRenderTexture>> retainedRenderTextures_;
        std::shared_ptr<const RenderShaderResources> shaderResources_;
    };

    class PlayerRenderFramePipeline final : public FrameRenderPipeline
    {
    public:
        explicit PlayerRenderFramePipeline(PlayerRenderFramePipelineInitParam initParam);

        void RenderFrame(const FrameRenderPipelineData& frameData) override;
        [[nodiscard]] const RenderShaderResources* GetShaderResources() const noexcept override;

    private:
        void EnsureSceneColorTexture(const FrameRenderPipelineData& frameData);

        BaseRendererInitParam sceneRenderer_;
        std::shared_ptr<RTRenderTexture> sceneColorTexture_;
        std::shared_ptr<const RenderShaderResources> shaderResources_;
    };
} // namespace ve
