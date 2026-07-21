#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/Atomic.h"

#include <memory>
#include <string>

namespace ve
{
    class VirtualShadowViewCache;

    /// Stable configuration for one logical render view across submitted frames.
    struct RenderViewStateDesc
    {
        std::string name = "RenderView";
        UInt32 virtualShadowAtlasExtent = 4096;
    };

    /// Render Thread proxy retained by frame pipelines while GPU work is in flight.
    class RTRenderViewState final : public NonCopyable
    {
    public:
        explicit RTRenderViewState(RenderViewStateDesc desc);

        [[nodiscard]] const RenderViewStateDesc& GetDesc() const noexcept;
        [[nodiscard]] UInt64 GetCameraCutRevision() const noexcept;

        // VirtualShadowViewCache ownership and access are added with the CPU cache implementation. Keeping that
        // extension out of this contract avoids exposing a callable accessor before the cache type is complete.

    private:
        friend class RenderViewState;

        void RequestCameraCut() noexcept;

        RenderViewStateDesc desc_;
        Atomic<UInt64> cameraCutRevision_{0};
    };

    /// Scene Thread owner for the persistent state of one logical render view.
    class RenderViewState final : public NonCopyable
    {
    public:
        explicit RenderViewState(RenderViewStateDesc desc);

        [[nodiscard]] std::shared_ptr<RTRenderViewState> GetRTRenderViewState() const noexcept;
        void RequestCameraCut() noexcept;

    private:
        std::shared_ptr<RTRenderViewState> rtViewState_;
    };
} // namespace ve
