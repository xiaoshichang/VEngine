#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderPerformanceStatistics.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include <memory>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

namespace ve
{
    class RTRenderViewState;
    class RTScene;
    class FrameGraph;
    struct FrameRenderPipelineData;
    struct RendererData;
    struct RendererFrameGraphData;

    namespace rhi
    {
        class RhiBuffer;
        class RhiFence;
        class RhiTexture;
    } // namespace rhi

    /// Frame-local prepared VSM data used to build an immutable sampling snapshot for one renderer view.
    struct VirtualShadowViewResult
    {
        VirtualShadowFramePacket packet;
        rhi::RhiTexture* atlas = nullptr;
        rhi::RhiBuffer* pageTable = nullptr;
        UInt64 pageTableOffset = 0;
        UInt64 pageTableSize = 0;
    };

    /// Read-only per-frame counters used to audit which VSM scheduling path owned a family.
    struct VirtualShadowSchedulingDiagnostics
    {
        UInt64 frameIndex = 0;
        UInt32 graphRegistrarInvocationCount = 0;
        UInt32 sceneStateAcquisitionCount = 0;
        UInt32 sceneFramePreparationCount = 0;
        UInt32 statisticsReadbackPreparationCount = 0;
        std::vector<VirtualShadowViewDrawDiagnostics> viewDraws;
    };

    namespace virtual_shadow_detail
    {
        class SceneState
        {
        public:
            [[nodiscard]] bool RegisterViewID(UInt32 viewID)
            {
                return !seenViewIDs_.insert(viewID).second;
            }

            void RequestReset() noexcept
            {
                ++resetGeneration_;
            }

            [[nodiscard]] bool IsResetPending() const noexcept
            {
                return consumedResetGeneration_ != resetGeneration_;
            }

            [[nodiscard]] UInt64 GetResetGeneration() const noexcept
            {
                return resetGeneration_;
            }

            void ConsumeReset() noexcept
            {
                consumedResetGeneration_ = resetGeneration_;
            }

        private:
            UInt64 resetGeneration_ = 0;
            UInt64 consumedResetGeneration_ = 0;
            std::unordered_set<UInt32> seenViewIDs_;
        };

        class FamilyViewClaims
        {
        public:
            [[nodiscard]] bool TryClaim(UInt32 viewID)
            {
                return claimedViewIDs_.insert(viewID).second;
            }

        private:
            std::unordered_set<UInt32> claimedViewIDs_;
        };

        inline void ApplyForcedViewInvalidation(VirtualShadowFramePacket& packet, bool& forceInvalidateViewPages) noexcept
        {
            if (!forceInvalidateViewPages || !packet.valid)
            {
                return;
            }
            packet.invalidateViewPages = true;
            forceInvalidateViewPages = false;
        }

        inline void QueueChangedCasterBounds(VirtualShadowViewCache& viewCache, UInt64 frameIndex, UInt32 viewID, std::span<const Aabb> changedCasterBounds)
        {
            (void)frameIndex;
            (void)viewID;
            viewCache.QueueChangedCasterBounds(changedCasterBounds);
        }
    } // namespace virtual_shadow_detail

    class VirtualShadowManager final : public NonCopyable
    {
    public:
        explicit VirtualShadowManager(VirtualShadowSceneCacheDesc desc = {});
        ~VirtualShadowManager();

        [[nodiscard]] UInt32 GetOrAssignViewID(RTRenderViewState& viewState) noexcept;
        /// Registers the production VSM graph after receiver depth and before scene shading.
        /// Every view must publish valid real VSM sampling bindings.
        void AddToFrameGraph(const FrameRenderPipelineData& frameData, FrameGraph& frameGraph, RendererData& rendererData, RendererFrameGraphData& graphData);
        [[nodiscard]] VirtualShadowSchedulingDiagnostics GetSchedulingDiagnostics(UInt64 frameIndex) const noexcept;
        [[nodiscard]] UInt64 GetRecordingSceneIdentity(UInt64 frameIndex) const noexcept;
        void NotifyFrameSubmitted(UInt64 frameIndex, rhi::RhiFence& completionFence, UInt64 fenceValue);
        [[nodiscard]] std::optional<RenderPerformanceStatistics> ConsumeCompletedFrameStatistics(rhi::RhiDevice& device, UInt64 frameIndex) noexcept;
        void CollectExpiredState();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace ve
