#include "Engine/Runtime/Render/RenderPass.h"

#include "Engine/Runtime/Core/Assert.h"

namespace ve
{
    void RenderPassBuilder::Reset(const char* passName, const RendererData& rendererData) noexcept
    {
        rendererData_ = &rendererData;
        renderPassDesc_ = {};
        renderPassDesc_.debugName = passName;
        renderPassDesc_.renderArea = rhi::RhiRenderArea{0, 0, rendererData.mainSurfaceExtent.width, rendererData.mainSurfaceExtent.height};

        viewport_ = rhi::RhiViewport{
            0.0f, 0.0f, static_cast<Float32>(rendererData.mainSurfaceExtent.width), static_cast<Float32>(rendererData.mainSurfaceExtent.height), 0.0f, 1.0f};
        scissorRect_ = rhi::RhiScissorRect{0, 0, rendererData.mainSurfaceExtent.width, rendererData.mainSurfaceExtent.height};
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

    RenderPassContext::RenderPassContext(rhi::RhiDevice& device,
                                         rhi::RhiCommandList& commandList,
                                         const RendererData& rendererData,
                                         const rhi::RhiRenderPassDesc& renderPassDesc,
                                         const rhi::RhiViewport& viewport,
                                         const rhi::RhiScissorRect& scissorRect) noexcept
        : device_(&device)
        , commandList_(&commandList)
        , rendererData_(&rendererData)
        , renderPassDesc_(&renderPassDesc)
        , viewport_(viewport)
        , scissorRect_(scissorRect)
    {
    }

    rhi::RhiDevice& RenderPassContext::GetDevice() noexcept
    {
        VE_ASSERT(device_ != nullptr);
        return *device_;
    }

    rhi::RhiCommandList& RenderPassContext::GetCommandList() noexcept
    {
        VE_ASSERT(commandList_ != nullptr);
        return *commandList_;
    }

    const RendererData& RenderPassContext::GetRendererData() const noexcept
    {
        VE_ASSERT(rendererData_ != nullptr);
        return *rendererData_;
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
