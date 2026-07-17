#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"

#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"

namespace ve
{
    FrameGraphBuilder::FrameGraphBuilder(FrameGraph& frameGraph, UInt32 passIndex) noexcept
        : frameGraph_(frameGraph)
        , passIndex_(passIndex)
    {
    }

    FrameGraphTextureHandle FrameGraphBuilder::Read(FrameGraphTextureHandle handle, FrameGraphTextureAccess access)
    {
        return frameGraph_.ReadTexture(passIndex_, handle, access);
    }

    FrameGraphTextureHandle FrameGraphBuilder::Write(FrameGraphTextureHandle handle, FrameGraphTextureAccess access)
    {
        return frameGraph_.WriteTexture(passIndex_, handle, access);
    }

    void FrameGraphBuilder::SetColorAttachment(FrameGraphTextureHandle handle,
                                               rhi::RhiLoadAction loadAction,
                                               rhi::RhiStoreAction storeAction,
                                               rhi::RhiColor clearColor)
    {
        frameGraph_.SetColorAttachment(passIndex_, handle, loadAction, storeAction, clearColor);
    }

    void FrameGraphBuilder::SetDepthAttachment(FrameGraphTextureHandle handle,
                                               rhi::RhiLoadAction loadAction,
                                               rhi::RhiStoreAction storeAction,
                                               rhi::RhiDepthStencilClearValue clearValue,
                                               bool readOnly)
    {
        frameGraph_.SetDepthAttachment(passIndex_, handle, loadAction, storeAction, clearValue, readOnly);
    }

    void FrameGraphBuilder::SetRenderArea(const rhi::RhiRenderArea& renderArea) noexcept
    {
        frameGraph_.SetRenderArea(passIndex_, renderArea);
    }

    void FrameGraphBuilder::SetViewport(const rhi::RhiViewport& viewport) noexcept
    {
        frameGraph_.SetViewport(passIndex_, viewport);
    }

    void FrameGraphBuilder::SetScissor(const rhi::RhiScissorRect& scissorRect) noexcept
    {
        frameGraph_.SetScissor(passIndex_, scissorRect);
    }

    void FrameGraphBuilder::SetSideEffect() noexcept
    {
        frameGraph_.SetSideEffect(passIndex_);
    }
} // namespace ve
