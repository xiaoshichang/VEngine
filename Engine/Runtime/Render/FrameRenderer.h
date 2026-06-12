#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderPass.h"

#include <memory>
#include <vector>

namespace ve
{
    /// Owns long-lived renderer passes and builds the per-frame pass data used by the Render Thread.
    class FrameRenderer : public NonCopyable
    {
    public:
        FrameRenderer() = default;
        ~FrameRenderer() = default;

        void AddPass(std::unique_ptr<RenderPass> pass);
        void ClearPasses() noexcept;

        [[nodiscard]] ErrorCode BuildFrameContext(rhi::RhiSwapchain& mainSwapchain) noexcept;
        void UpdateRenderWorld();
        void BuildVisibleDrawLists();

        [[nodiscard]] ErrorCode BeginFrame(rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain);
        [[nodiscard]] ErrorCode ExecutePassesInOrder();
        void EndFrame();

        [[nodiscard]] bool IsFrameActive() const noexcept;
        [[nodiscard]] const RenderFrameContext& GetFrameContext() const noexcept;

    private:
        struct FramePassData
        {
            RenderPass* pass = nullptr;
            rhi::RhiRenderPassDesc renderPassDesc = {};
            rhi::RhiViewport viewport = {};
            rhi::RhiScissorRect scissorRect = {};
        };

        [[nodiscard]] ErrorCode BuildPassData();
        [[nodiscard]] ErrorCode BeginCurrentPass(rhi::RhiSwapchain& mainSwapchain);

        std::vector<std::unique_ptr<RenderPass>> passes_;
        std::vector<FramePassData> framePasses_;
        RenderFrameContext frameContext_ = {};
        rhi::RhiCommandList* activeCommandList_ = nullptr;
        rhi::RhiSwapchain* activeMainSwapchain_ = nullptr;
        UInt32 activePassIndex_ = 0;
        bool frameActive_ = false;
        bool renderPassOpen_ = false;
    };
} // namespace ve
