#include "Engine/Runtime/Render/RenderViewState.h"

#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include <utility>

namespace ve
{
    RTRenderViewState::RTRenderViewState(RenderViewStateDesc desc)
        : desc_(std::move(desc))
        , virtualShadowViewCache_(std::make_unique<VirtualShadowViewCache>())
    {
    }

    RTRenderViewState::~RTRenderViewState() = default;

    const RenderViewStateDesc& RTRenderViewState::GetDesc() const noexcept
    {
        return desc_;
    }

    UInt32 RTRenderViewState::GetVirtualShadowViewID() const noexcept
    {
        return virtualShadowViewID_;
    }

    bool RTRenderViewState::TryAssignVirtualShadowViewID(UInt32 viewID) noexcept
    {
        if (viewID == InvalidVirtualShadowViewID || viewID > VirtualShadowMaximumViewID)
        {
            return false;
        }
        if (virtualShadowViewID_ != InvalidVirtualShadowViewID && virtualShadowViewID_ != viewID)
        {
            return false;
        }

        virtualShadowViewID_ = viewID;
        return true;
    }

    const VirtualShadowPageTableSlice& RTRenderViewState::GetVirtualShadowPageTableSlice() const noexcept
    {
        return virtualShadowPageTableSlice_;
    }

    void RTRenderViewState::SetVirtualShadowPageTableSlice(VirtualShadowPageTableSlice slice) noexcept
    {
        virtualShadowPageTableSlice_ = slice;
    }

    void RTRenderViewState::ClearVirtualShadowPageTableSlice() noexcept
    {
        virtualShadowPageTableSlice_ = {};
    }

    VirtualShadowViewCache& RTRenderViewState::GetVirtualShadowViewCache() noexcept
    {
        return *virtualShadowViewCache_;
    }

    const VirtualShadowViewCache& RTRenderViewState::GetVirtualShadowViewCache() const noexcept
    {
        return *virtualShadowViewCache_;
    }

    RenderViewState::RenderViewState(RenderViewStateDesc desc)
        : rtViewState_(std::make_shared<RTRenderViewState>(std::move(desc)))
    {
    }

    std::shared_ptr<RTRenderViewState> RenderViewState::GetRTRenderViewState() const noexcept
    {
        return rtViewState_;
    }

} // namespace ve
