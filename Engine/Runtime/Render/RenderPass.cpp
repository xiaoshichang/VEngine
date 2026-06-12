#include "Engine/Runtime/Render/RenderPass.h"

#include "Engine/Runtime/Core/Assert.h"

namespace ve
{
    void RenderPassBuilder::Reset(const char* passName, const RenderFrameContext& frameContext) noexcept
    {
        renderPassDesc_ = {};
        renderPassDesc_.debugName = passName;
        renderPassDesc_.renderArea = rhi::RhiRenderArea{0,
                                                        0,
                                                        frameContext.mainSurfaceExtent.width,
                                                        frameContext.mainSurfaceExtent.height};

        viewport_ = rhi::RhiViewport{0.0f,
                                     0.0f,
                                     static_cast<Float32>(frameContext.mainSurfaceExtent.width),
                                     static_cast<Float32>(frameContext.mainSurfaceExtent.height),
                                     0.0f,
                                     1.0f};
        scissorRect_ = rhi::RhiScissorRect{0,
                                           0,
                                           frameContext.mainSurfaceExtent.width,
                                           frameContext.mainSurfaceExtent.height};
    }

    void RenderPassBuilder::SetRenderArea(const rhi::RhiRenderArea& renderArea) noexcept
    {
        renderPassDesc_.renderArea = renderArea;
    }

    void RenderPassBuilder::SetViewport(const rhi::RhiViewport& viewport) noexcept
    {
        viewport_ = viewport;
    }

    void RenderPassBuilder::SetScissor(const rhi::RhiScissorRect& scissorRect) noexcept
    {
        scissorRect_ = scissorRect;
    }

    void RenderPassBuilder::AddSwapchainColorAttachment(rhi::RhiLoadAction loadAction,
                                                        rhi::RhiStoreAction storeAction,
                                                        rhi::RhiColor clearColor) noexcept
    {
        VE_ASSERT_MESSAGE(renderPassDesc_.colorAttachmentCount < rhi::RhiMaxColorAttachments,
                          "RenderPassBuilder color attachment count exceeded RhiMaxColorAttachments.");

        rhi::RhiRenderPassColorAttachmentDesc& attachment =
            renderPassDesc_.colorAttachments[renderPassDesc_.colorAttachmentCount];
        attachment.texture = nullptr;
        attachment.loadAction = loadAction;
        attachment.storeAction = storeAction;
        attachment.clearColor = clearColor;
        ++renderPassDesc_.colorAttachmentCount;
    }

    const rhi::RhiRenderPassDesc& RenderPassBuilder::GetRenderPassDesc() const noexcept
    {
        return renderPassDesc_;
    }

    const rhi::RhiViewport& RenderPassBuilder::GetViewport() const noexcept
    {
        return viewport_;
    }

    const rhi::RhiScissorRect& RenderPassBuilder::GetScissor() const noexcept
    {
        return scissorRect_;
    }

    RenderPassContext::RenderPassContext(rhi::RhiCommandList& commandList,
                                         const RenderFrameContext& frameContext,
                                         const rhi::RhiRenderPassDesc& renderPassDesc,
                                         const rhi::RhiViewport& viewport,
                                         const rhi::RhiScissorRect& scissorRect) noexcept
        : commandList_(&commandList)
        , frameContext_(&frameContext)
        , renderPassDesc_(&renderPassDesc)
        , viewport_(viewport)
        , scissorRect_(scissorRect)
    {
    }

    rhi::RhiCommandList& RenderPassContext::GetCommandList() noexcept
    {
        VE_ASSERT(commandList_ != nullptr);
        return *commandList_;
    }

    const RenderFrameContext& RenderPassContext::GetFrameContext() const noexcept
    {
        VE_ASSERT(frameContext_ != nullptr);
        return *frameContext_;
    }

    const rhi::RhiRenderPassDesc& RenderPassContext::GetRenderPassDesc() const noexcept
    {
        VE_ASSERT(renderPassDesc_ != nullptr);
        return *renderPassDesc_;
    }

    const rhi::RhiViewport& RenderPassContext::GetViewport() const noexcept
    {
        return viewport_;
    }

    const rhi::RhiScissorRect& RenderPassContext::GetScissor() const noexcept
    {
        return scissorRect_;
    }
} // namespace ve
