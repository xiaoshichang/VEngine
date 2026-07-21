#pragma once

#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h"

#include <span>
#include <vector>

namespace ve
{
    class RTCamera;
    class RTRenderItem;
    class RTScene;

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
        bool castShadows = true;
        bool receiveShadows = true;
        const RTRenderItem* renderItem = nullptr;
    };

    struct VirtualShadowPrepareInput
    {
        UInt64 frameIndex = 0;
        UInt64 cameraCutRevision = 0;
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
    };

    class VirtualShadowViewCache
    {
    public:
        explicit VirtualShadowViewCache(UInt32 atlasExtent);

        [[nodiscard]] VirtualShadowFramePacket PrepareFrame(const VirtualShadowPrepareInput& input);
        [[nodiscard]] VirtualShadowFramePacket
        PrepareFrame(UInt64 frameIndex, UInt64 cameraCutRevision, const RTCamera& camera, const RTScene& scene, UInt32 targetWidth, UInt32 targetHeight);
        void MarkRendered(std::span<const VirtualShadowPageKey> keys);

        [[nodiscard]] UInt32 GetAtlasExtent() const noexcept;
        [[nodiscard]] VirtualShadowPageCache& GetPageCache() noexcept;
        [[nodiscard]] const VirtualShadowPageCache& GetPageCache() const noexcept;

    private:
        UInt32 atlasExtent_ = 0;
        VirtualShadowPageCache pageCache_;
        VirtualShadowInvalidationTracker invalidationTracker_;
        UInt64 lastCameraCutRevision_ = 0;
        bool hasCameraCutRevision_ = false;
    };
} // namespace ve
