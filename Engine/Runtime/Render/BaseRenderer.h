#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderPass.h"
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
        rhi::RhiColor clearColor{0.05f, 0.07f, 0.10f, 1.0f};
    };

    /// Describes a forward renderer instance for one scene view.
    ///
    /// `scene` is the render-thread scene snapshot source. `camera` optionally overrides the scene's primary camera,
    /// which is how editor viewports provide their own SceneView camera. `target` chooses the output surface,
    /// `fillMode` controls rasterization, and `additionalPasses` allows small renderer extensions while keeping the
    /// standard opaque pass optional.
    struct ForwardRendererDesc
    {
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTCamera> camera;
        RendererRenderTarget target;
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
        bool addOpaquePass = true;
        std::vector<std::unique_ptr<RenderPass>> additionalPasses;
    };

    /// Renders one RTScene through a configurable list of long-lived render passes.
    class BaseRenderer : public NonCopyable
    {
    public:
        BaseRenderer() = default;
        virtual ~BaseRenderer() = default;

        /// Records all render passes for this renderer into an already-begun command list.
        ///
        /// The caller owns the command list frame lifetime (`Begin`/`End`) and the main swapchain. `BaseRenderer`
        /// builds a per-frame context, lets each pass declare its attachments, begins each render pass, executes it,
        /// and closes it before returning. Renderer failures are treated as engine invariants: this function asserts
        /// and safely stops recording the renderer instead of returning an `ErrorCode`.
        void RenderScene(const FrameRenderPipelineData& frameData);

        /// Returns true only while RenderScene is actively building or recording passes.
        ///
        /// This is mainly a guard for renderer mutation APIs such as AddRenderPass and ClearRenderPasses.
        [[nodiscard]] bool IsFrameActive() const noexcept;

        /// Returns the renderer data built for the currently active scene render, or the most recently built data.
        ///
        /// Render passes should prefer the RenderPassContext they receive during execution. This accessor exists for
        /// diagnostics and tightly-scoped renderer extensions that need to inspect the current renderer state.
        [[nodiscard]] const RendererData& GetRendererData() const noexcept;

    protected:
        /// Replaces the RTScene that future frames will render.
        ///
        /// The scene pointer is shared with frame pipelines and must reference render-thread-facing state, not live
        /// GameObject component data.
        void SetScene(std::shared_ptr<RTScene> scene) noexcept;

        /// Sets an optional camera override for this renderer.
        ///
        /// When null, the renderer resolves the primary scene camera from the RTScene. Editor views use this to render
        /// the same scene from a viewport-owned camera without mutating the scene camera list.
        void SetOverrideCamera(std::shared_ptr<RTCamera> camera) noexcept;

        /// Sets the rasterizer fill mode used by passes that honor renderer-wide fill state.
        void SetFillMode(rhi::RhiFillMode fillMode) noexcept;

        /// Returns the currently assigned render scene.
        [[nodiscard]] std::shared_ptr<RTScene> GetScene() const noexcept;

        /// Appends a render pass to this renderer.
        ///
        /// Must be called before RenderScene begins. Pass order is preserved and each pass gets a fresh
        /// RenderPassBuilder during frame setup.
        void AddRenderPass(std::unique_ptr<RenderPass> pass);

        /// Removes all configured passes.
        ///
        /// Must be called outside an active frame. This is useful for renderer variants that rebuild pass topology.
        void ClearRenderPasses() noexcept;

    private:
        void BuildRendererData(const FrameRenderPipelineData& frameData) noexcept;
        void UpdateRenderWorld();
        void BuildVisibleDrawLists();
        void BeginSceneRender(const FrameRenderPipelineData& frameData);
        void ExecutePassesInOrder();
        void EndSceneRender();
        void BuildPassData();
        void BeginCurrentPass();

        std::shared_ptr<RTScene> scene_;
        std::shared_ptr<RTCamera> overrideCamera_;
        std::vector<std::unique_ptr<RenderPass>> passes_;
        std::vector<RenderPassData> renderPassData_;
        RendererData rendererData_ = {};
        rhi::RhiFillMode fillMode_ = rhi::RhiFillMode::Solid;
    };

    class ForwardRenderer final : public BaseRenderer
    {
    public:
        explicit ForwardRenderer(ForwardRendererDesc desc);
    };
} // namespace ve
