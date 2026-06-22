#include "Engine/Runtime/Render/RenderPass/RenderPass.h"

#include "Engine/Runtime/Core/Assert.h"
namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiDevice& ResolveDevice(const FrameRenderPipelineData& frameData) noexcept
        {
            VE_ASSERT(frameData.device != nullptr);
            return *frameData.device;
        }

        [[nodiscard]] rhi::RhiCommandList& ResolveCommandList(const FrameRenderPipelineData& frameData) noexcept
        {
            VE_ASSERT(frameData.commandList != nullptr);
            return *frameData.commandList;
        }
    } // namespace

    RenderPassBuilder::RenderPassBuilder(RenderPassBuilderInitParam initParam) noexcept
        : frameData(initParam.frameData)
        , rendererData(initParam.rendererData)
    {
        VE_ASSERT(frameData.mainSwapchain != nullptr);
        renderPassDesc_.debugName = initParam.passName;

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

    RenderPassData RenderPassBuilder::Build() const noexcept
    {
        RenderPassData passData = {};
        passData.renderPassDesc = renderPassDesc_;
        passData.viewport = viewport_;
        passData.scissorRect = scissorRect_;
        return passData;
    }

    RenderPassContext::RenderPassContext(RenderPassContextInitParam initParam) noexcept
        : frameData(initParam.frameData)
        , rendererData(initParam.rendererData)
        , passData(initParam.passData)
        , device(ResolveDevice(initParam.frameData))
        , commandList(ResolveCommandList(initParam.frameData))
    {
    }
} // namespace ve
