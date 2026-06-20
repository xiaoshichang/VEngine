#include "Engine/Runtime/Render/RenderPass/RenderPass.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/BaseRenderer.h"

namespace ve
{
    void RenderPassBuilder::Reset(const char* passName, const FrameRenderPipelineData& frameData, const RendererData& rendererData) noexcept
    {
        VE_ASSERT(frameData.mainSwapchain != nullptr);

        frameData_ = &frameData;
        rendererData_ = &rendererData;
        renderPassDesc_ = {};
        renderPassDesc_.debugName = passName;

        const rhi::RhiExtent2D mainSurfaceExtent = frameData.mainSwapchain->GetExtent();
        renderPassDesc_.renderArea = rhi::RhiRenderArea{0, 0, mainSurfaceExtent.width, mainSurfaceExtent.height};

        viewport_ = rhi::RhiViewport{
            0.0f, 0.0f, static_cast<Float32>(mainSurfaceExtent.width), static_cast<Float32>(mainSurfaceExtent.height), 0.0f, 1.0f};
        scissorRect_ = rhi::RhiScissorRect{0, 0, mainSurfaceExtent.width, mainSurfaceExtent.height};
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

    void RenderPassBuilder::AddSwapchainColorAttachment(rhi::RhiLoadAction loadAction, rhi::RhiStoreAction storeAction, rhi::RhiColor clearColor) noexcept
    {
        VE_ASSERT_MESSAGE(renderPassDesc_.colorAttachmentCount < rhi::RhiMaxColorAttachments,
                          "RenderPassBuilder color attachment count exceeded RhiMaxColorAttachments.");

        rhi::RhiRenderPassColorAttachmentDesc& attachment = renderPassDesc_.colorAttachments[renderPassDesc_.colorAttachmentCount];
        attachment.texture = nullptr;
        attachment.loadAction = loadAction;
        attachment.storeAction = storeAction;
        attachment.clearColor = clearColor;
        ++renderPassDesc_.colorAttachmentCount;
    }

    void RenderPassBuilder::AddTextureColorAttachment(rhi::RhiTexture& texture,
                                                      rhi::RhiLoadAction loadAction,
                                                      rhi::RhiStoreAction storeAction,
                                                      rhi::RhiColor clearColor) noexcept
    {
        VE_ASSERT_MESSAGE(renderPassDesc_.colorAttachmentCount < rhi::RhiMaxColorAttachments,
                          "RenderPassBuilder color attachment count exceeded RhiMaxColorAttachments.");

        rhi::RhiRenderPassColorAttachmentDesc& attachment = renderPassDesc_.colorAttachments[renderPassDesc_.colorAttachmentCount];
        attachment.texture = &texture;
        attachment.loadAction = loadAction;
        attachment.storeAction = storeAction;
        attachment.clearColor = clearColor;
        ++renderPassDesc_.colorAttachmentCount;

        const UInt32 width = texture.GetWidth();
        const UInt32 height = texture.GetHeight();
        renderPassDesc_.renderArea = rhi::RhiRenderArea{0, 0, width, height};
        viewport_ = rhi::RhiViewport{0.0f, 0.0f, static_cast<Float32>(width), static_cast<Float32>(height), 0.0f, 1.0f};
        scissorRect_ = rhi::RhiScissorRect{0, 0, width, height};
    }

    void RenderPassBuilder::SetDepthStencilAttachment(rhi::RhiTexture& texture,
                                                      rhi::RhiLoadAction depthLoadAction,
                                                      rhi::RhiStoreAction depthStoreAction,
                                                      rhi::RhiDepthStencilClearValue clearValue) noexcept
    {
        rhi::RhiRenderPassDepthStencilAttachmentDesc& attachment = renderPassDesc_.depthStencilAttachment;
        attachment.texture = &texture;
        attachment.depthLoadAction = depthLoadAction;
        attachment.depthStoreAction = depthStoreAction;
        attachment.clearValue = clearValue;
        renderPassDesc_.hasDepthStencilAttachment = true;
    }

    const rhi::RhiRenderPassDesc& RenderPassBuilder::GetRenderPassDesc() const noexcept
    {
        return renderPassDesc_;
    }

    const FrameRenderPipelineData& RenderPassBuilder::GetFrameData() const noexcept
    {
        VE_ASSERT(frameData_ != nullptr);
        return *frameData_;
    }

    const RendererData& RenderPassBuilder::GetRendererData() const noexcept
    {
        VE_ASSERT(rendererData_ != nullptr);
        return *rendererData_;
    }

    const rhi::RhiViewport& RenderPassBuilder::GetViewport() const noexcept
    {
        return viewport_;
    }

    const rhi::RhiScissorRect& RenderPassBuilder::GetScissor() const noexcept
    {
        return scissorRect_;
    }

    RenderPassContext::RenderPassContext(const FrameRenderPipelineData& frameData,
                                         BaseRenderer& renderer,
                                         const RenderPassData& passData) noexcept
        : frameData_(&frameData)
        , renderer_(&renderer)
        , passData_(&passData)
    {
    }

    const FrameRenderPipelineData& RenderPassContext::GetFrameData() const noexcept
    {
        VE_ASSERT(frameData_ != nullptr);
        return *frameData_;
    }

    BaseRenderer& RenderPassContext::GetRenderer() noexcept
    {
        VE_ASSERT(renderer_ != nullptr);
        return *renderer_;
    }

    rhi::RhiDevice& RenderPassContext::GetDevice() noexcept
    {
        VE_ASSERT(frameData_ != nullptr);
        VE_ASSERT(frameData_->device != nullptr);
        return *frameData_->device;
    }

    rhi::RhiCommandList& RenderPassContext::GetCommandList() noexcept
    {
        VE_ASSERT(frameData_ != nullptr);
        VE_ASSERT(frameData_->commandList != nullptr);
        return *frameData_->commandList;
    }

    const RendererData& RenderPassContext::GetRendererData() const noexcept
    {
        VE_ASSERT(renderer_ != nullptr);
        return renderer_->GetRendererData();
    }

    const RenderPassData& RenderPassContext::GetPassData() const noexcept
    {
        VE_ASSERT(passData_ != nullptr);
        return *passData_;
    }

    const rhi::RhiRenderPassDesc& RenderPassContext::GetRenderPassDesc() const noexcept
    {
        VE_ASSERT(passData_ != nullptr);
        return passData_->renderPassDesc;
    }

    const rhi::RhiViewport& RenderPassContext::GetViewport() const noexcept
    {
        VE_ASSERT(passData_ != nullptr);
        return passData_->viewport;
    }

    const rhi::RhiScissorRect& RenderPassContext::GetScissor() const noexcept
    {
        VE_ASSERT(passData_ != nullptr);
        return passData_->scissorRect;
    }
} // namespace ve
