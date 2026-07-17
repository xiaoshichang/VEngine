#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"

#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"

namespace ve
{
    FrameGraphBuilder::FrameGraphBuilder(FrameGraph& frameGraph, UInt32 passIndex) noexcept
        : frameGraph_(frameGraph)
        , passIndex_(passIndex)
    {
    }

    FrameGraphTextureHandle FrameGraphBuilder::Read(FrameGraphTextureHandle handle)
    {
        return frameGraph_.ReadTexture(passIndex_, handle, FrameGraphTextureAccess::ShaderRead);
    }

    FrameGraphTextureHandle FrameGraphBuilder::WriteColorAttachment(FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, rhi::RhiColor clearColor)
    {
        return frameGraph_.WriteColorAttachment(passIndex_, handle, loadAction, clearColor);
    }

    FrameGraphTextureHandle FrameGraphBuilder::WriteDepthAttachment(FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, Float32 clearDepth)
    {
        return frameGraph_.WriteDepthAttachment(passIndex_, handle, loadAction, clearDepth);
    }

    FrameGraphTextureHandle FrameGraphBuilder::ReadDepthAttachment(FrameGraphTextureHandle handle)
    {
        return frameGraph_.ReadDepthAttachment(passIndex_, handle);
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

    const RendererData& FrameGraphBuilder::GetRendererData() const noexcept
    {
        return frameGraph_.GetRendererData();
    }
} // namespace ve
