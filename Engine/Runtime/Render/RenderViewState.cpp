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
