#pragma once

#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ve
{
    class RTCamera;
    class RTRenderItem;
    class RTScene;

    namespace rhi
    {
        class RhiBuffer;
        class RhiDevice;
        class RhiSampler;
        class RhiTexture;
    } // namespace rhi

    struct VirtualShadowLightInput
    {
        bool enabled = false;
        Vector3 direction = Vector3::UnitZ();
        Float32 shadowDistance = 200.0f;
        Float32 depthBias = 0.001f;
        Float32 normalBias = 0.05f;
        UInt64 revision = 0;
    };

    struct VirtualShadowSceneItem
    {
        UInt64 renderItemID = 0;
        UInt64 revision = 0;
        Aabb worldBounds = Aabb(Vector3::Zero(), Vector3::Zero());
        bool opaque = true;
        bool castShadows = true;
        bool receiveShadows = true;
        const RTRenderItem* renderItem = nullptr;
    };

    struct VirtualShadowPrepareInput
    {
        UInt64 frameIndex = 0;
        UInt64 cameraCutRevision = 0;
        UInt64 virtualShadowCacheRevision = 0;
        UInt32 screenWidth = 0;
        UInt32 screenHeight = 0;
        Matrix44 viewProjection = Matrix44::Identity();
        Matrix44 cameraLocalToWorld = Matrix44::Identity();
        VirtualShadowLightInput light;
        std::span<const VirtualShadowSceneItem> items;
    };

    struct VirtualShadowDirtyPageDraw
    {
        VirtualShadowPageKey key;
        UInt32 physicalPageIndex = InvalidVirtualShadowPhysicalPage;
        VirtualShadowPhysicalPageOrigin physicalOrigin;
        Matrix44 pageViewProjection = Matrix44::Identity();
        std::vector<UInt64> casterRenderItemIDs;
        std::vector<const RTRenderItem*> casters;
    };

    struct VirtualShadowStatistics
    {
        UInt32 requested = 0;
        UInt32 resident = 0;
        UInt32 allocated = 0;
        UInt32 cached = 0;
        UInt32 dirty = 0;
        UInt32 rendered = 0;
        UInt32 evicted = 0;
        UInt32 missing = 0;
        UInt32 casterDraws = 0;
    };

    struct VirtualShadowFramePacket
    {
        bool valid = true;
        bool enabled = false;
        UInt32 atlasExtent = 0;
        Float32 depthBias = 0.0f;
        Float32 normalBias = 0.0f;
        VirtualShadowClipmapSet clipmaps;
        VirtualShadowPageTable residentPageTable;
        std::vector<VirtualShadowDirtyPageDraw> dirtyPages;
        VirtualShadowStatistics statistics;
        bool gpuDriven = false;
        bool resetGpuCache = false;
        UInt64 frameIndex = 0;
        UInt32 screenWidth = 0;
        UInt32 screenHeight = 0;
        Matrix44 inverseViewProjection = Matrix44::Identity();
        Vector4 cameraWorldPosition = Vector4::Zero();
        Vector4 cameraWorldForward = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
        std::vector<VirtualShadowPageKey> invalidatedPageKeys;
        bool invalidateAllGpuPages = false;
    };

    class VirtualShadowViewCache
    {
    public:
        explicit VirtualShadowViewCache(UInt32 atlasExtent);
        ~VirtualShadowViewCache();

        [[nodiscard]] VirtualShadowFramePacket PrepareFrame(const VirtualShadowPrepareInput& input);
        [[nodiscard]] VirtualShadowFramePacket
        PrepareFrame(UInt64 frameIndex,
                     UInt64 cameraCutRevision,
                     UInt64 virtualShadowCacheRevision,
                     const RTCamera& camera,
                     const RTScene& scene,
                     UInt32 targetWidth,
                     UInt32 targetHeight);
        [[nodiscard]] VirtualShadowFramePacket PrepareGpuFrame(const VirtualShadowPrepareInput& input);
        [[nodiscard]] VirtualShadowFramePacket
        PrepareGpuFrame(UInt64 frameIndex,
                        UInt64 cameraCutRevision,
                        UInt64 virtualShadowCacheRevision,
                        const RTCamera& camera,
                        const RTScene& scene,
                        UInt32 targetWidth,
                        UInt32 targetHeight);
        [[nodiscard]] bool EnsureSamplingPageTable(rhi::RhiDevice& device, const std::string& viewName);
        [[nodiscard]] bool EnsureGpuResources(rhi::RhiDevice& device, const std::string& viewName);
        [[nodiscard]] bool CanUseGpuDriven(const rhi::RhiDevice& device) const noexcept;
        void DisableGpuDriven() noexcept;
        void MarkRendered(std::span<const VirtualShadowPageKey> keys);

        [[nodiscard]] UInt32 GetAtlasExtent() const noexcept;
        [[nodiscard]] rhi::RhiTexture* GetAtlasTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetAtlasTexture() const noexcept;
        [[nodiscard]] rhi::RhiSampler* GetComparisonSampler() noexcept;
        [[nodiscard]] const rhi::RhiSampler* GetComparisonSampler() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuPageMarksBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuPageTableBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuRequestListBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuRequestCountsBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetSamplingPageTableBuffer() noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetGpuPhysicalPagesBuffer() noexcept;
        [[nodiscard]] UInt32 GetGpuPhysicalPageCapacity() const noexcept;
        [[nodiscard]] bool ConsumeGpuCacheReset() noexcept;
        [[nodiscard]] VirtualShadowPageCache& GetPageCache() noexcept;
        [[nodiscard]] const VirtualShadowPageCache& GetPageCache() const noexcept;

    private:
        void ApplyVirtualShadowCacheRevision(UInt64 revision) noexcept;

        UInt32 atlasExtent_ = 0;
        VirtualShadowPageCache pageCache_;
        VirtualShadowInvalidationTracker invalidationTracker_;
        UInt64 lastCameraCutRevision_ = 0;
        bool hasCameraCutRevision_ = false;
        UInt64 lastVirtualShadowCacheRevision_ = 0;
        bool hasVirtualShadowCacheRevision_ = false;
        Float32 lastShadowDistance_ = 0.0f;
        bool hasShadowDistance_ = false;
        rhi::RhiDevice* resourceDevice_ = nullptr;
        std::unique_ptr<rhi::RhiTexture> atlasTexture_;
        std::unique_ptr<rhi::RhiSampler> comparisonSampler_;
        std::unique_ptr<rhi::RhiBuffer> gpuPageMarksBuffer_;
        std::unique_ptr<rhi::RhiBuffer> gpuPageTableBuffer_;
        std::unique_ptr<rhi::RhiBuffer> gpuRequestListBuffer_;
        std::unique_ptr<rhi::RhiBuffer> gpuRequestCountsBuffer_;
        std::unique_ptr<rhi::RhiBuffer> samplingPageTableBuffer_;
        std::unique_ptr<rhi::RhiBuffer> gpuPhysicalPagesBuffer_;
        bool gpuDrivenAvailable_ = true;
        bool gpuCacheResetPending_ = true;
    };

    [[nodiscard]] VirtualShadowGpuConstants BuildVirtualShadowGpuConstants(const VirtualShadowFramePacket& packet) noexcept;
} // namespace ve
