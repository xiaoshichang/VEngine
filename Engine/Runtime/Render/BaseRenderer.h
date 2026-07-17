#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>
#include <vector>

namespace ve
{
    class ShaderManager;

    /// Describes where a renderer should write its main color output.
    ///
    /// `colorTexture` is optional. When it is set, the renderer writes into the texture-backed render target and can
    /// use the texture-owned depth attachment. When it is null, the renderer targets the frame pipeline's main
    /// swapchain. Load/store settings describe the color attachment for the first opaque pass.
    struct RendererRenderTarget
    {
        std::shared_ptr<RTRenderTexture> colorTexture;
        rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
        rhi::RhiStoreAction colorStoreAction = rhi::RhiStoreAction::Store;
    };

    struct BaseRendererInitParam
    {
        const FrameRenderPipelineData* frameData = nullptr;
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTCamera> camera;
        std::vector<std::unique_ptr<RenderPass>> passes;
    };

    /// Describes a forward renderer instance for one scene view.
    ///
    /// `scene` is the render-thread scene snapshot source. `camera` is selected by the Scene or supplied by an editor
    /// viewport before render-thread submission. `target` chooses the output surface,
    /// `fillMode` controls rasterization, and inherited `passes` contains extra passes recorded after the optional
    /// standard opaque pass.
    struct ForwardRendererInitParam : public BaseRendererInitParam
    {
        RendererRenderTarget target;
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
        bool addOpaquePass = true;

        [[nodiscard]] BaseRendererInitParam TakeBaseInitParam() &&;
    };

    /// Renders one RTScene through a configurable list of long-lived render passes.
    class BaseRenderer : public NonCopyable
    {
    public:
        virtual ~BaseRenderer() = default;

        /// Records all render passes for this renderer into an already-begun command list.
        ///
        /// The caller owns the command list frame lifetime (`Begin`/`End`). FrameRenderPipelineData is captured by the
        /// renderer init param so the renderer has all frame-scoped dependencies before RenderScene begins.
        void RenderScene();

    protected:
        explicit BaseRenderer(BaseRendererInitParam initParam);

    private:
        void UpdateRenderWorld();
        void BuildVisibleDrawLists();
        void ExecutePassesInOrder();
        [[nodiscard]] RenderPassData BuildPassData(RenderPass& pass);
        [[nodiscard]] bool BeginPass(const RenderPassData& passData);

        std::vector<std::unique_ptr<RenderPass>> passes_;
        RendererData rendererData_ = {};
        const FrameRenderPipelineData* frameRenderData_ = nullptr;
    };

    class ForwardRenderer final : public BaseRenderer
    {
    public:
        explicit ForwardRenderer(ForwardRendererInitParam initParam);
    };
} // namespace ve
