#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"

#include <memory>
#include <string>

namespace ve
{
    class VirtualShadowManager;
    class VirtualShadowViewCache;

    /// Stable configuration for one logical render view across submitted frames.
    struct RenderViewStateDesc
    {
        std::string name = "RenderView";
    };

    /// Render Thread proxy retained by frame pipelines while GPU work is in flight.
    class RTRenderViewState final : public NonCopyable
    {
    public:
        explicit RTRenderViewState(RenderViewStateDesc desc);
        ~RTRenderViewState();

        [[nodiscard]] const RenderViewStateDesc& GetDesc() const noexcept;
        [[nodiscard]] UInt32 GetVirtualShadowViewID() const noexcept;
        [[nodiscard]] bool TryAssignVirtualShadowViewID(UInt32 viewID) noexcept;
        [[nodiscard]] const VirtualShadowPageTableSlice& GetVirtualShadowPageTableSlice() const noexcept;
        [[nodiscard]] VirtualShadowViewCache& GetVirtualShadowViewCache() noexcept;
        [[nodiscard]] const VirtualShadowViewCache& GetVirtualShadowViewCache() const noexcept;

    private:
        friend class VirtualShadowManager;

        void SetVirtualShadowPageTableSlice(VirtualShadowPageTableSlice slice) noexcept;
        void ClearVirtualShadowPageTableSlice() noexcept;

    private:
        struct VirtualShadowViewLifetimeToken
        {
        };

        RenderViewStateDesc desc_;
        std::shared_ptr<const VirtualShadowViewLifetimeToken> virtualShadowLifetimeToken_ = std::make_shared<const VirtualShadowViewLifetimeToken>();
        UInt32 virtualShadowViewID_ = InvalidVirtualShadowViewID;
        VirtualShadowPageTableSlice virtualShadowPageTableSlice_;
        std::unique_ptr<VirtualShadowViewCache> virtualShadowViewCache_;
    };

    /// Scene Thread owner for the persistent state of one logical render view.
    class RenderViewState final : public NonCopyable
    {
    public:
        explicit RenderViewState(RenderViewStateDesc desc);

        [[nodiscard]] std::shared_ptr<RTRenderViewState> GetRTRenderViewState() const noexcept;

    private:
        std::shared_ptr<RTRenderViewState> rtViewState_;
    };
} // namespace ve
