#include "Engine/Runtime/Render/FrameRenderer.h"

#include "Engine/Runtime/Core/Assert.h"

#include <utility>

namespace ve
{
    void FrameRenderer::AddPass(std::unique_ptr<RenderPass> pass)
    {
        VE_ASSERT_MESSAGE(!frameActive_, "FrameRenderer::AddPass requires no active frame.");
        VE_ASSERT_MESSAGE(pass != nullptr, "FrameRenderer::AddPass requires a valid pass.");
        passes_.push_back(std::move(pass));
    }

    void FrameRenderer::ClearPasses() noexcept
    {
        VE_ASSERT_MESSAGE(!frameActive_, "FrameRenderer::ClearPasses requires no active frame.");
        passes_.clear();
        framePasses_.clear();
    }

    ErrorCode FrameRenderer::BuildFrameContext(rhi::RhiSwapchain& mainSwapchain) noexcept
    {
        frameContext_.mainSurfaceExtent = mainSwapchain.GetExtent();
        ++frameContext_.frameIndex;
        return ErrorCode::None;
    }

    void FrameRenderer::UpdateRenderWorld()
    {
        // The first render slice has no render proxies yet. Game Thread snapshots will be consumed here later.
    }

    void FrameRenderer::BuildVisibleDrawLists()
    {
        // Visibility and draw-list construction will move here once mesh render proxies exist.
    }

    ErrorCode FrameRenderer::BeginFrame(rhi::RhiCommandList& commandList, rhi::RhiSwapchain& mainSwapchain)
    {
        if (frameActive_)
        {
            return ErrorCode::InvalidState;
        }

        ErrorCode buildContextResult = BuildFrameContext(mainSwapchain);
        if (buildContextResult != ErrorCode::None)
        {
            return buildContextResult;
        }

        UpdateRenderWorld();
        BuildVisibleDrawLists();

        ErrorCode buildPassResult = BuildPassData();
        if (buildPassResult != ErrorCode::None)
        {
            return buildPassResult;
        }

        if (!commandList.Begin())
        {
            return ErrorCode::PlatformError;
        }

        activeCommandList_ = &commandList;
        activeMainSwapchain_ = &mainSwapchain;
        activePassIndex_ = 0;
        frameActive_ = true;
        renderPassOpen_ = false;

        if (!framePasses_.empty())
        {
            ErrorCode beginPassResult = BeginCurrentPass(mainSwapchain);
            if (beginPassResult != ErrorCode::None)
            {
                activeCommandList_ = nullptr;
                activeMainSwapchain_ = nullptr;
                frameActive_ = false;
                return beginPassResult;
            }
        }

        return ErrorCode::None;
    }

    ErrorCode FrameRenderer::ExecutePassesInOrder()
    {
        if (!frameActive_ || activeCommandList_ == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        while (activePassIndex_ < framePasses_.size())
        {
            if (!renderPassOpen_)
            {
                VE_ASSERT(activeMainSwapchain_ != nullptr);
                ErrorCode beginPassResult = BeginCurrentPass(*activeMainSwapchain_);
                if (beginPassResult != ErrorCode::None)
                {
                    return beginPassResult;
                }
            }

            FramePassData& passData = framePasses_[activePassIndex_];
            VE_ASSERT(passData.pass != nullptr);

            RenderPassContext passContext(*activeCommandList_,
                                          frameContext_,
                                          passData.renderPassDesc,
                                          passData.viewport,
                                          passData.scissorRect);
            passData.pass->Execute(passContext);

            activeCommandList_->EndRenderPass();
            renderPassOpen_ = false;
            ++activePassIndex_;
        }

        return ErrorCode::None;
    }

    void FrameRenderer::EndFrame()
    {
        VE_ASSERT_MESSAGE(frameActive_, "FrameRenderer::EndFrame requires an active frame.");
        VE_ASSERT(activeCommandList_ != nullptr);

        if (renderPassOpen_)
        {
            activeCommandList_->EndRenderPass();
            renderPassOpen_ = false;
        }

        const bool ended = activeCommandList_->End();
        VE_ASSERT_MESSAGE(ended, "FrameRenderer failed to end the active RHI command list.");

        activeCommandList_ = nullptr;
        activeMainSwapchain_ = nullptr;
        activePassIndex_ = 0;
        frameActive_ = false;
    }

    bool FrameRenderer::IsFrameActive() const noexcept
    {
        return frameActive_;
    }

    const RenderFrameContext& FrameRenderer::GetFrameContext() const noexcept
    {
        return frameContext_;
    }

    ErrorCode FrameRenderer::BuildPassData()
    {
        framePasses_.clear();
        framePasses_.reserve(passes_.size());

        RenderPassBuilder builder;
        for (const std::unique_ptr<RenderPass>& pass : passes_)
        {
            VE_ASSERT(pass != nullptr);
            builder.Reset(pass->GetName(), frameContext_);
            pass->Setup(builder);

            FramePassData framePass = {};
            framePass.pass = pass.get();
            framePass.renderPassDesc = builder.GetRenderPassDesc();
            framePass.viewport = builder.GetViewport();
            framePass.scissorRect = builder.GetScissor();
            framePasses_.push_back(framePass);
        }

        return ErrorCode::None;
    }

    ErrorCode FrameRenderer::BeginCurrentPass(rhi::RhiSwapchain& mainSwapchain)
    {
        VE_ASSERT(frameActive_);
        VE_ASSERT(activeCommandList_ != nullptr);
        VE_ASSERT(activePassIndex_ < framePasses_.size());
        VE_ASSERT(!renderPassOpen_);

        const FramePassData& passData = framePasses_[activePassIndex_];
        if (!activeCommandList_->BeginRenderPass(mainSwapchain, passData.renderPassDesc))
        {
            return ErrorCode::PlatformError;
        }

        activeCommandList_->SetViewport(passData.viewport);
        activeCommandList_->SetScissor(passData.scissorRect);
        renderPassOpen_ = true;
        return ErrorCode::None;
    }
} // namespace ve
