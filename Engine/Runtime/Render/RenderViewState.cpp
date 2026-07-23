#include "Engine/Runtime/Render/RenderViewState.h"

#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include <utility>

namespace ve
{
    RTRenderViewState::RTRenderViewState(RenderViewStateDesc desc)
        : desc_(std::move(desc))
        , virtualShadowViewCache_(std::make_unique<VirtualShadowViewCache>(desc_.virtualShadowAtlasExtent))
    {
    }

    RTRenderViewState::~RTRenderViewState() = default;

    const RenderViewStateDesc& RTRenderViewState::GetDesc() const noexcept
    {
        return desc_;
    }

    UInt64 RTRenderViewState::GetCameraCutRevision() const noexcept
    {
        return cameraCutRevision_.load(std::memory_order_acquire);
    }

    UInt64 RTRenderViewState::GetVirtualShadowCacheRevision() const noexcept
    {
        return virtualShadowCacheRevision_.load(std::memory_order_acquire);
    }

    VirtualShadowViewCache& RTRenderViewState::GetVirtualShadowViewCache() noexcept
    {
        return *virtualShadowViewCache_;
    }

    const VirtualShadowViewCache& RTRenderViewState::GetVirtualShadowViewCache() const noexcept
    {
        return *virtualShadowViewCache_;
    }

    void RTRenderViewState::RequestCameraCut() noexcept
    {
        cameraCutRevision_.fetch_add(1, std::memory_order_acq_rel);
    }

    void RTRenderViewState::RequestVirtualShadowCacheReset() noexcept
    {
        virtualShadowCacheRevision_.fetch_add(1, std::memory_order_acq_rel);
    }

    RenderViewState::RenderViewState(RenderViewStateDesc desc)
        : rtViewState_(std::make_shared<RTRenderViewState>(std::move(desc)))
    {
    }

    std::shared_ptr<RTRenderViewState> RenderViewState::GetRTRenderViewState() const noexcept
    {
        return rtViewState_;
    }

    void RenderViewState::RequestCameraCut() noexcept
    {
        rtViewState_->RequestCameraCut();
    }

    void RenderViewState::RequestVirtualShadowCacheReset() noexcept
    {
        rtViewState_->RequestVirtualShadowCacheReset();
    }
} // namespace ve
