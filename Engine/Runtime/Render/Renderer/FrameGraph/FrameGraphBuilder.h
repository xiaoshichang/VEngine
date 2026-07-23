#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"

namespace ve
{
    class FrameGraph;
    struct RendererData;

    /// Declares resource access and raster state for one frame-graph pass.
    class FrameGraphBuilder final : public NonCopyable
    {
    public:
        /// Declares a shader read of an existing logical texture version.
        [[nodiscard]] FrameGraphTextureHandle Read(FrameGraphTextureHandle handle);

        /// Declares a shader read of an existing logical buffer version.
        [[nodiscard]] FrameGraphBufferHandle Read(FrameGraphBufferHandle handle);

        /// Declares a shader read-write access and returns the newly written logical buffer version.
        [[nodiscard]] FrameGraphBufferHandle Write(FrameGraphBufferHandle handle);

        /// Declares the pass color output and returns the newly written logical version.
        [[nodiscard]] FrameGraphTextureHandle
        WriteColorAttachment(FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, rhi::RhiColor clearColor = {});

        /// Declares a writable depth output and returns the newly written logical version.
        [[nodiscard]] FrameGraphTextureHandle WriteDepthAttachment(FrameGraphTextureHandle handle, rhi::RhiLoadAction loadAction, Float32 clearDepth = 1.0f);

        /// Declares an existing depth version as a read-only attachment.
        [[nodiscard]] FrameGraphTextureHandle ReadDepthAttachment(FrameGraphTextureHandle handle);

        void SetRenderArea(const rhi::RhiRenderArea& renderArea) noexcept;
        void SetViewport(const rhi::RhiViewport& viewport) noexcept;
        void SetScissor(const rhi::RhiScissorRect& scissorRect) noexcept;
        void SetSideEffect() noexcept;
        [[nodiscard]] const RendererData& GetRendererData() const noexcept;

    private:
        friend class FrameGraph;

        FrameGraphBuilder(FrameGraph& frameGraph, UInt32 passIndex) noexcept;

        FrameGraph& frameGraph_;
        UInt32 passIndex_ = 0;
    };
} // namespace ve
