#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/RenderScene.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace ve
{
    namespace
    {
        Aabb TransformBoundsToLightSpace(const VirtualShadowLightBasis& basis, const Aabb& worldBounds)
        {
            const Vector3& minimum = worldBounds.GetMinimum();
            const Vector3& maximum = worldBounds.GetMaximum();
            Vector3 lightMinimum(std::numeric_limits<Float32>::max(), std::numeric_limits<Float32>::max(), std::numeric_limits<Float32>::max());
            Vector3 lightMaximum(std::numeric_limits<Float32>::lowest(), std::numeric_limits<Float32>::lowest(), std::numeric_limits<Float32>::lowest());
            for (UInt32 cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
            {
                const Vector3 corner((cornerIndex & 1u) != 0u ? maximum.GetX() : minimum.GetX(),
                                     (cornerIndex & 2u) != 0u ? maximum.GetY() : minimum.GetY(),
                                     (cornerIndex & 4u) != 0u ? maximum.GetZ() : minimum.GetZ());
                const Vector3 lightCorner = basis.TransformPoint(corner);
                lightMinimum.SetX(std::min(lightMinimum.GetX(), lightCorner.GetX()));
                lightMinimum.SetY(std::min(lightMinimum.GetY(), lightCorner.GetY()));
                lightMinimum.SetZ(std::min(lightMinimum.GetZ(), lightCorner.GetZ()));
                lightMaximum.SetX(std::max(lightMaximum.GetX(), lightCorner.GetX()));
                lightMaximum.SetY(std::max(lightMaximum.GetY(), lightCorner.GetY()));
                lightMaximum.SetZ(std::max(lightMaximum.GetZ(), lightCorner.GetZ()));
            }
            return Aabb(lightMinimum, lightMaximum);
        }

        Matrix44 BuildPageViewProjection(const VirtualShadowLightBasis& basis, const Aabb& pageBounds) noexcept
        {
            const Vector3& minimum = pageBounds.GetMinimum();
            const Vector3& maximum = pageBounds.GetMaximum();
            Matrix44 worldToLight = Matrix44::Identity();
            worldToLight.Set(0, 0, basis.right.GetX());
            worldToLight.Set(0, 1, basis.right.GetY());
            worldToLight.Set(0, 2, basis.right.GetZ());
            worldToLight.Set(1, 0, basis.up.GetX());
            worldToLight.Set(1, 1, basis.up.GetY());
            worldToLight.Set(1, 2, basis.up.GetZ());
            worldToLight.Set(2, 0, basis.forward.GetX());
            worldToLight.Set(2, 1, basis.forward.GetY());
            worldToLight.Set(2, 2, basis.forward.GetZ());

            const Float32 width = maximum.GetX() - minimum.GetX();
            const Float32 height = maximum.GetY() - minimum.GetY();
            const Float32 depth = maximum.GetZ() - minimum.GetZ();
            Matrix44 projection = Matrix44::Zero();
            projection.Set(0, 0, 2.0f / width);
            projection.Set(0, 3, -(maximum.GetX() + minimum.GetX()) / width);
            projection.Set(1, 1, 2.0f / height);
            projection.Set(1, 3, -(maximum.GetY() + minimum.GetY()) / height);
            projection.Set(2, 2, 1.0f / depth);
            projection.Set(2, 3, -minimum.GetZ() / depth);
            projection.Set(3, 3, 1.0f);
            return projection * worldToLight;
        }
    } // namespace

    VirtualShadowViewCache::VirtualShadowViewCache(UInt32 atlasExtent)
        : atlasExtent_(atlasExtent)
        , pageCache_(std::min(GetVirtualShadowPhysicalPageCapacity(atlasExtent), VirtualShadowPageTableCapacity))
    {
    }

    VirtualShadowFramePacket VirtualShadowViewCache::PrepareFrame(const VirtualShadowPrepareInput& input)
    {
        VirtualShadowFramePacket packet;
        packet.atlasExtent = atlasExtent_;
        pageCache_.BeginFrame(input.frameIndex);

        if (!hasCameraCutRevision_)
        {
            lastCameraCutRevision_ = input.cameraCutRevision;
            hasCameraCutRevision_ = true;
        }
        else if (lastCameraCutRevision_ != input.cameraCutRevision)
        {
            pageCache_.ClearRequestHistory();
            lastCameraCutRevision_ = input.cameraCutRevision;
        }

        if (pageCache_.GetCapacity() == 0 || !input.light.enabled)
        {
            return packet;
        }

        if (!hasShadowDistance_)
        {
            lastShadowDistance_ = input.light.shadowDistance;
            hasShadowDistance_ = true;
        }
        else if (!NearlyEqual(lastShadowDistance_, input.light.shadowDistance))
        {
            pageCache_.InvalidateAll();
            lastShadowDistance_ = input.light.shadowDistance;
        }

        packet.clipmaps = BuildVirtualShadowClipmaps(input.cameraLocalToWorld, input.light.direction, input.light.shadowDistance);
        if (!packet.clipmaps.valid)
        {
            return packet;
        }

        packet.enabled = true;
        packet.depthBias = input.light.depthBias;
        packet.normalBias = input.light.normalBias;

        std::vector<VirtualShadowReceiver> receivers;
        std::vector<VirtualShadowCasterSnapshot> casters;
        receivers.reserve(input.items.size());
        casters.reserve(input.items.size());
        for (const VirtualShadowSceneItem& item : input.items)
        {
            if (!item.worldBounds.IsFiniteAndValid())
            {
                continue;
            }
            if (item.receiveShadows)
            {
                receivers.push_back({item.renderItemID, item.worldBounds, true});
            }
            if (item.castShadows)
            {
                casters.push_back({item.renderItemID, item.revision, item.worldBounds, true});
            }
        }

        const VirtualShadowInvalidationResult invalidation = invalidationTracker_.Update(input.frameIndex, packet.clipmaps, input.light.direction, casters);
        if (invalidation.fullInvalidation)
        {
            pageCache_.InvalidateAll();
        }
        for (VirtualShadowPageKey key : invalidation.invalidatedKeys)
        {
            (void)pageCache_.Invalidate(key);
        }

        const VirtualShadowRequestBuildInput requestInput{input.viewProjection, input.cameraLocalToWorld, packet.clipmaps, receivers};
        const std::vector<VirtualShadowPageRequest> requests = BuildVirtualShadowPageRequests(requestInput);
        const VirtualShadowRequestResolution resolution = pageCache_.ResolveRequests(requests);

        const std::vector<UInt32> dirtyPageIndices = pageCache_.GetDirtyPhysicalPageIndices();
        const std::span<const VirtualShadowPhysicalPage> physicalPages = pageCache_.GetPhysicalPages();
        packet.dirtyPages.reserve(dirtyPageIndices.size());
        for (UInt32 physicalPageIndex : dirtyPageIndices)
        {
            const VirtualShadowPhysicalPage& physicalPage = physicalPages[physicalPageIndex];
            const VirtualShadowClipmapLevel& level = packet.clipmaps.levels[physicalPage.key.GetClipmapLevel()];
            const Float32 gutterWorldSize =
                level.pageWorldSize * static_cast<Float32>(VirtualShadowPageGutter) / static_cast<Float32>(VirtualShadowPhysicalPageContentSize);
            const Aabb pageBounds = GetVirtualShadowPageLightSpaceBounds(packet.clipmaps, physicalPage.key, gutterWorldSize);

            VirtualShadowDirtyPageDraw draw;
            draw.key = physicalPage.key;
            draw.physicalPageIndex = physicalPageIndex;
            draw.physicalOrigin = GetVirtualShadowPhysicalPageOrigin(physicalPageIndex, atlasExtent_);
            draw.pageViewProjection = BuildPageViewProjection(packet.clipmaps.lightBasis, pageBounds);
            for (const VirtualShadowSceneItem& item : input.items)
            {
                if (!item.castShadows || !item.worldBounds.IsFiniteAndValid() ||
                    !TransformBoundsToLightSpace(packet.clipmaps.lightBasis, item.worldBounds).Intersects(pageBounds))
                {
                    continue;
                }
                draw.casterRenderItemIDs.push_back(item.renderItemID);
                if (item.renderItem != nullptr)
                {
                    draw.casters.push_back(item.renderItem);
                }
            }
            packet.statistics.casterDraws += static_cast<UInt32>(draw.casterRenderItemIDs.size());
            packet.dirtyPages.push_back(std::move(draw));
        }

        packet.residentPageTable = pageCache_.BuildResidentPageTable();
        packet.statistics.requested = resolution.requested;
        packet.statistics.resident = pageCache_.GetResidentPageCount();
        packet.statistics.allocated = resolution.allocated;
        packet.statistics.cached = pageCache_.GetCachedPageCount();
        packet.statistics.dirty = pageCache_.GetDirtyPageCount();
        packet.statistics.evicted = resolution.evicted;
        packet.statistics.missing = resolution.missing;
        return packet;
    }

    VirtualShadowFramePacket VirtualShadowViewCache::PrepareFrame(
        UInt64 frameIndex, UInt64 cameraCutRevision, const RTCamera& camera, const RTScene& scene, UInt32 targetWidth, UInt32 targetHeight)
    {
        VirtualShadowLightInput light;
        for (SizeT lightIndex = 0; lightIndex < scene.GetLightCount(); ++lightIndex)
        {
            const std::shared_ptr<RTLight> sceneLight = scene.GetLight(lightIndex);
            if (sceneLight != nullptr && sceneLight->GetType() == RTLightType::Directional && sceneLight->CastShadows())
            {
                light = VirtualShadowLightInput{true,
                                                sceneLight->GetDirection(),
                                                sceneLight->GetShadowDistance(),
                                                sceneLight->GetDepthBias(),
                                                sceneLight->GetNormalBias(),
                                                sceneLight->GetShadowRevision()};
                break;
            }
        }

        std::vector<VirtualShadowSceneItem> items;
        items.reserve(scene.GetRenderItemCount());
        for (SizeT itemIndex = 0; itemIndex < scene.GetRenderItemCount(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem> renderItem = scene.GetRenderItem(itemIndex);
            if (renderItem == nullptr)
            {
                continue;
            }
            const Aabb localBounds = Aabb::FromCenterExtents(renderItem->GetBoundsCenter(), renderItem->GetBoundsExtents());
            items.push_back({renderItem->GetRenderItemID(),
                             renderItem->GetRevision(),
                             localBounds.Transformed(renderItem->GetLocalToWorld()),
                             renderItem->CastShadows(),
                             renderItem->ReceiveShadows(),
                             renderItem.get()});
        }

        VirtualShadowPrepareInput input;
        input.frameIndex = frameIndex;
        input.cameraCutRevision = cameraCutRevision;
        input.cameraLocalToWorld = camera.GetLocalToWorld();
        input.viewProjection = BuildCameraViewProjection(camera, rhi::RhiExtent2D{targetWidth, targetHeight});
        input.light = light;
        input.items = items;
        return PrepareFrame(input);
    }

    void VirtualShadowViewCache::MarkRendered(std::span<const VirtualShadowPageKey> keys)
    {
        pageCache_.MarkRendered(keys);
    }

    UInt32 VirtualShadowViewCache::GetAtlasExtent() const noexcept
    {
        return atlasExtent_;
    }

    VirtualShadowPageCache& VirtualShadowViewCache::GetPageCache() noexcept
    {
        return pageCache_;
    }

    const VirtualShadowPageCache& VirtualShadowViewCache::GetPageCache() const noexcept
    {
        return pageCache_;
    }
} // namespace ve
