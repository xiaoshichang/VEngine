#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

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

        void AppendInvalidationKeys(std::unordered_set<VirtualShadowPageKey, VirtualShadowPageKeyHash>& keys,
                                    const VirtualShadowClipmapSet& clipmaps,
                                    const Aabb& bounds)
        {
            const std::vector<VirtualShadowPageKey> boundKeys = BuildVirtualShadowPageKeysForBounds(clipmaps, bounds);
            keys.insert(boundKeys.begin(), boundKeys.end());
        }
    } // namespace

    std::vector<VirtualShadowPageKey> BuildVirtualShadowPageKeysForBounds(const VirtualShadowClipmapSet& clipmaps, const Aabb& worldBounds)
    {
        if (!clipmaps.valid || !worldBounds.IsFiniteAndValid())
        {
            return {};
        }

        const Aabb lightBounds = TransformBoundsToLightSpace(clipmaps.lightBasis, worldBounds);
        std::unordered_set<VirtualShadowPageKey, VirtualShadowPageKeyHash> uniqueKeys;
        for (UInt32 levelIndex = 0; levelIndex < VirtualShadowClipmapLevelCount; ++levelIndex)
        {
            const VirtualShadowClipmapLevel& level = clipmaps.levels[levelIndex];
            const Float32 depthCenter = static_cast<Float32>(level.depthEpoch) * clipmaps.depthStep;
            if (lightBounds.GetMaximum().GetZ() < depthCenter - clipmaps.shadowDistance ||
                lightBounds.GetMinimum().GetZ() > depthCenter + clipmaps.shadowDistance)
            {
                continue;
            }

            const Int32 workingMinimumX = level.originPageX - static_cast<Int32>(VirtualShadowPagesPerAxis / 2u);
            const Int32 workingMinimumY = level.originPageY - static_cast<Int32>(VirtualShadowPagesPerAxis / 2u);
            const Int32 workingMaximumX = workingMinimumX + static_cast<Int32>(VirtualShadowPagesPerAxis) - 1;
            const Int32 workingMaximumY = workingMinimumY + static_cast<Int32>(VirtualShadowPagesPerAxis) - 1;
            const Int32 minimumPageX = std::max(static_cast<Int32>(std::floor(lightBounds.GetMinimum().GetX() / level.pageWorldSize)), workingMinimumX);
            const Int32 minimumPageY = std::max(static_cast<Int32>(std::floor(lightBounds.GetMinimum().GetY() / level.pageWorldSize)), workingMinimumY);
            const Int32 maximumPageX = std::min(static_cast<Int32>(std::floor(lightBounds.GetMaximum().GetX() / level.pageWorldSize)), workingMaximumX);
            const Int32 maximumPageY = std::min(static_cast<Int32>(std::floor(lightBounds.GetMaximum().GetY() / level.pageWorldSize)), workingMaximumY);
            for (Int32 pageY = minimumPageY; pageY <= maximumPageY; ++pageY)
            {
                for (Int32 pageX = minimumPageX; pageX <= maximumPageX; ++pageX)
                {
                    const VirtualShadowPageKey key = VirtualShadowPageKey::Create(pageX, pageY, levelIndex, level.depthEpoch);
                    if (key.IsValid())
                    {
                        uniqueKeys.insert(key);
                    }
                }
            }
        }

        std::vector<VirtualShadowPageKey> result(uniqueKeys.begin(), uniqueKeys.end());
        std::ranges::sort(result,
                          [](VirtualShadowPageKey left, VirtualShadowPageKey right)
                          { return left.key1 != right.key1 ? left.key1 < right.key1 : left.key0 < right.key0; });
        return result;
    }

    VirtualShadowInvalidationResult VirtualShadowInvalidationTracker::Update(UInt64 frameIndex,
                                                                             const VirtualShadowClipmapSet& clipmaps,
                                                                             Vector3 lightDirection,
                                                                             std::span<const VirtualShadowCasterSnapshot> casters)
    {
        VirtualShadowInvalidationResult result;
        lightDirection = lightDirection.Normalized();
        if (hasLightDirection_ && !lastLightDirection_.IsNearlyEqual(lightDirection))
        {
            result.fullInvalidation = true;
        }
        lastLightDirection_ = lightDirection;
        hasLightDirection_ = true;

        std::unordered_set<VirtualShadowPageKey, VirtualShadowPageKeyHash> invalidatedKeys;
        for (const VirtualShadowCasterSnapshot& caster : casters)
        {
            if (!caster.castShadows || !caster.worldBounds.IsFiniteAndValid())
            {
                continue;
            }

            auto [iterator, inserted] =
                trackedCasters_.try_emplace(caster.renderItemID, VirtualShadowTrackedCaster{caster.revision, caster.worldBounds, frameIndex});
            if (inserted)
            {
                AppendInvalidationKeys(invalidatedKeys, clipmaps, caster.worldBounds);
                continue;
            }

            VirtualShadowTrackedCaster& tracked = iterator->second;
            if (tracked.revision != caster.revision)
            {
                AppendInvalidationKeys(invalidatedKeys, clipmaps, tracked.worldBounds);
                AppendInvalidationKeys(invalidatedKeys, clipmaps, caster.worldBounds);
                tracked.revision = caster.revision;
                tracked.worldBounds = caster.worldBounds;
            }
            tracked.lastSeenFrame = frameIndex;
        }

        for (auto iterator = trackedCasters_.begin(); iterator != trackedCasters_.end();)
        {
            if (iterator->second.lastSeenFrame != frameIndex)
            {
                AppendInvalidationKeys(invalidatedKeys, clipmaps, iterator->second.worldBounds);
                iterator = trackedCasters_.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }

        result.invalidatedKeys.assign(invalidatedKeys.begin(), invalidatedKeys.end());
        std::ranges::sort(result.invalidatedKeys,
                          [](VirtualShadowPageKey left, VirtualShadowPageKey right)
                          { return left.key1 != right.key1 ? left.key1 < right.key1 : left.key0 < right.key0; });
        return result;
    }

    void VirtualShadowInvalidationTracker::Clear() noexcept
    {
        trackedCasters_.clear();
        lastLightDirection_ = Vector3::Zero();
        hasLightDirection_ = false;
    }

    UInt32 VirtualShadowInvalidationTracker::GetTrackedCasterCount() const noexcept
    {
        return static_cast<UInt32>(trackedCasters_.size());
    }
} // namespace ve
