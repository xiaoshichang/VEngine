#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderPass.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>
#include <vector>

namespace ve
{
    struct RendererRenderTarget
    {
        std::shared_ptr<RTRenderTexture> colorTexture;
        rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
        rhi::RhiStoreAction colorStoreAction = rhi::RhiStoreAction::Store;
        rhi::RhiColor clearColor{0.05f, 0.07f, 0.10f, 1.0f};
    };

    struct ForwardRendererDesc
    {
        std::shared_ptr<RTScene> scene;
        RendererRenderTarget target;
        bool addOpaquePass = true;
        std::vector<std::unique_ptr<RenderPass>> additionalPasses;
    };

    /// Renders one RTScene through a configurable list of long-lived render passes.
    class BaseRenderer : public NonCopyable
    {
    public:
        BaseRenderer() = default;
        virtual ~BaseRenderer() = default;

        [[nodiscard]] ErrorCode RenderScene(rhi::RhiDevice& device, rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain);

        [[nodiscard]] bool IsFrameActive() const noexcept;
        [[nodiscard]] const RenderFrameContext& GetFrameContext() const noexcept;

    protected:
        void SetScene(std::shared_ptr<RTScene> scene) noexcept;
        [[nodiscard]] std::shared_ptr<RTScene> GetScene() const noexcept;
        void AddRenderPass(std::unique_ptr<RenderPass> pass);
        void ClearRenderPasses() noexcept;

    private:
        struct FramePassData
        {
            RenderPass* pass = nullptr;
            rhi::RhiRenderPassDesc renderPassDesc = {};
            rhi::RhiViewport viewport = {};
            rhi::RhiScissorRect scissorRect = {};
        };

        [[nodiscard]] ErrorCode BuildFrameContext(rhi::RhiSwapchain& mainSwapchain) noexcept;
        void UpdateRenderWorld();
        void BuildVisibleDrawLists();
        [[nodiscard]] ErrorCode BeginSceneRender(rhi::RhiDevice& device, rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain);
        [[nodiscard]] ErrorCode ExecutePassesInOrder();
        void EndSceneRender();
        [[nodiscard]] ErrorCode BuildPassData();
        [[nodiscard]] ErrorCode BeginCurrentPass(rhi::RhiSwapchain& mainSwapchain);

        std::shared_ptr<RTScene> scene_;
        std::vector<std::unique_ptr<RenderPass>> passes_;
        std::vector<FramePassData> framePasses_;
        RenderFrameContext frameContext_ = {};
        rhi::RhiDevice* activeDevice_ = nullptr;
        rhi::RhiCommandList* activeCommandList_ = nullptr;
        rhi::RhiSwapchain* activeMainSwapchain_ = nullptr;
        UInt32 activePassIndex_ = 0;
        bool frameActive_ = false;
        bool renderPassOpen_ = false;
    };

    class ForwardRenderer final : public BaseRenderer
    {
    public:
        explicit ForwardRenderer(ForwardRendererDesc desc);
    };
} // namespace ve
