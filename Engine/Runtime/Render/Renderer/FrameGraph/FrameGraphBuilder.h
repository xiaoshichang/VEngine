#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"

namespace ve
{
    class FrameGraph;

    /// Declares resource access and raster state for one frame-graph pass.
    class FrameGraphBuilder final : public NonCopyable
    {
    public:
        [[nodiscard]] FrameGraphTextureHandle Read(FrameGraphTextureHandle handle, FrameGraphTextureAccess access);
        [[nodiscard]] FrameGraphTextureHandle Write(FrameGraphTextureHandle handle, FrameGraphTextureAccess access);

        void SetColorAttachment(FrameGraphTextureHandle handle,
                                rhi::RhiLoadAction loadAction,
                                rhi::RhiStoreAction storeAction,
                                rhi::RhiColor clearColor);
        void SetDepthAttachment(FrameGraphTextureHandle handle,
                                rhi::RhiLoadAction loadAction,
                                rhi::RhiStoreAction storeAction,
                                rhi::RhiDepthStencilClearValue clearValue,
                                bool readOnly);
        void SetRenderArea(const rhi::RhiRenderArea& renderArea) noexcept;
        void SetViewport(const rhi::RhiViewport& viewport) noexcept;
        void SetScissor(const rhi::RhiScissorRect& scissorRect) noexcept;
        void SetSideEffect() noexcept;

    private:
        friend class FrameGraph;

        FrameGraphBuilder(FrameGraph& frameGraph, UInt32 passIndex) noexcept;

        FrameGraph& frameGraph_;
        UInt32 passIndex_ = 0;
    };
} // namespace ve
