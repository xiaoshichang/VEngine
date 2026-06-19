#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderPass.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <functional>
#include <memory>
#include <vector>

namespace ve
{
    using EditorOverlayRenderCallback = std::function<void()>;

    struct EditorRendererDesc
    {
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTRenderTexture> gameViewTexture;
        rhi::RhiLoadAction mainColorLoadAction = rhi::RhiLoadAction::Clear;
        EditorOverlayRenderCallback overlayRenderCallback;
    };

    struct PlayerRendererDesc
    {
        std::shared_ptr<RTScene> scene;
    };

    /// Owns per-frame renderer orchestration and long-lived pass instances.
    class BaseRenderer : public NonCopyable
    {
    public:
        BaseRenderer() = default;
        virtual ~BaseRenderer() = default;

        [[nodiscard]] ErrorCode RenderFrame(rhi::RhiDevice& device,
                                            rhi::RhiCommandList& commandList,
                                            rhi::RhiSwapchain& mainSwapchain);

        [[nodiscard]] bool IsFrameActive() const noexcept;
        [[nodiscard]] const RenderFrameContext& GetFrameContext() const noexcept;

    protected:
        void SetScene(std::shared_ptr<RTScene> scene) noexcept;
        [[nodiscard]] std::shared_ptr<RTScene> GetScene() const noexcept;
        void AddInternalPass(std::unique_ptr<RenderPass> pass);
        void ClearInternalPasses() noexcept;

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
        [[nodiscard]] ErrorCode BeginFrame(rhi::RhiDevice& device,
                                           rhi::RhiCommandList& commandList,
                                           rhi::RhiSwapchain& mainSwapchain);
        [[nodiscard]] ErrorCode ExecutePassesInOrder();
        void EndFrame();
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

    class PlayerRenderer final : public BaseRenderer
    {
    public:
        explicit PlayerRenderer(PlayerRendererDesc desc);
    };

    class EditorRenderer final : public BaseRenderer
    {
    public:
        explicit EditorRenderer(EditorRendererDesc desc);
    };
} // namespace ve
