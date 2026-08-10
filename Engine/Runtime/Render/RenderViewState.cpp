#include "Engine/Runtime/Render/RenderViewState.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

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

    UniformBufferAllocation RTRenderViewState::GetViewUniform(rhi::RhiDevice& device,
                                                              UInt32 frameSlotIndex,
                                                              UInt64 frameIndex,
                                                              const RTCamera* camera,
                                                              rhi::RhiExtent2D targetExtent)
    {
        VE_ASSERT_RENDER_THREAD();
        if (lastUniformFrameIndex_ == frameIndex)
        {
            VE_ASSERT_MESSAGE(lastUniformCamera_ == camera && lastUniformTargetExtent_.width == targetExtent.width &&
                                  lastUniformTargetExtent_.height == targetExtent.height,
                              "One RTRenderViewState cannot use different camera or extent values in the same frame.");
        }
        else
        {
            lastUniformFrameIndex_ = frameIndex;
            lastUniformCamera_ = camera;
            lastUniformTargetExtent_ = targetExtent;
        }

        const ViewUniformData data = BuildViewUniformData(camera, targetExtent);
        return viewUniformBuffer_.GetOrUpdate(device, frameSlotIndex, &data, sizeof(data), frameIndex, "RTRenderViewStateUniform");
    }

    RhiObjectList RTRenderViewState::TakeRhiObjects() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        lastUniformFrameIndex_ = 0;
        lastUniformCamera_ = nullptr;
        lastUniformTargetExtent_ = {};
        return viewUniformBuffer_.TakeRhiObjects();
    }

    RenderViewState::RenderViewState(RenderSystem& renderSystem, RenderViewStateDesc desc)
        : rtViewState_(std::make_shared<RTRenderViewState>(std::move(desc)))
        , renderSystem_(&renderSystem)
    {
    }

    RenderViewState::~RenderViewState()
    {
        if (renderSystem_ == nullptr || rtViewState_ == nullptr || !renderSystem_->IsInitialized())
        {
            return;
        }

        try
        {
            renderSystem_->ReleaseRenderResource(std::move(rtViewState_));
        }
        catch (...)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "RenderViewState failed to enqueue render resource release.");
        }
    }

    std::shared_ptr<RTRenderViewState> RenderViewState::GetRTRenderViewState() const noexcept
    {
        return rtViewState_;
    }

} // namespace ve
