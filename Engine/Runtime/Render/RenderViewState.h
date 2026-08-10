#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"

#include <memory>
#include <string>

namespace ve
{
    class VirtualShadowManager;
    class VirtualShadowViewCache;
    class RenderSystem;
    class RTCamera;

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
        [[nodiscard]] UniformBufferAllocation GetViewUniform(rhi::RhiDevice& device,
                                                             UInt32 frameSlotIndex,
                                                             UInt64 frameIndex,
                                                             const RTCamera* camera,
                                                             rhi::RhiExtent2D targetExtent);
        [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

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
        RTDynamicUniformBuffer viewUniformBuffer_;
        UInt64 lastUniformFrameIndex_ = 0;
        const RTCamera* lastUniformCamera_ = nullptr;
        rhi::RhiExtent2D lastUniformTargetExtent_{};
    };

    /// Scene Thread owner for the persistent state of one logical render view.
    class RenderViewState final : public NonCopyable
    {
    public:
        RenderViewState(RenderSystem& renderSystem, RenderViewStateDesc desc);
        ~RenderViewState();

        [[nodiscard]] std::shared_ptr<RTRenderViewState> GetRTRenderViewState() const noexcept;

    private:
        std::shared_ptr<RTRenderViewState> rtViewState_;
        RenderSystem* renderSystem_ = nullptr;
    };
} // namespace ve
