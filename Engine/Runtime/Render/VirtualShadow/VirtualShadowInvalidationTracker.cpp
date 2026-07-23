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

        bool AppendInvalidationKeys(std::unordered_set<VirtualShadowPageKey, VirtualShadowPageKeyHash>& keys,
                                    const VirtualShadowClipmapSet& clipmaps,
                                    const Aabb& bounds,
                                    VirtualShadowInvalidationCoverage coverage,
                                    SizeT maximumInvalidatedKeys)
        {
            if (coverage == VirtualShadowInvalidationCoverage::CurrentWorkingRegion)
            {
                const std::vector<VirtualShadowPageKey> boundKeys = BuildVirtualShadowPageKeysForBounds(clipmaps, bounds);
                keys.insert(boundKeys.begin(), boundKeys.end());
                return keys.size() <= maximumInvalidatedKeys;
            }

            if (!clipmaps.valid || !bounds.IsFiniteAndValid())
            {
                return true;
            }

            const Aabb lightBounds = TransformBoundsToLightSpace(clipmaps.lightBasis, bounds);
            for (UInt32 levelIndex = 0; levelIndex < VirtualShadowClipmapLevelCount; ++levelIndex)
            {
                const VirtualShadowClipmapLevel& level = clipmaps.levels[levelIndex];
                Int32 minimumPageX = 0;
                Int32 minimumPageY = 0;
                Int32 maximumPageX = 0;
                Int32 maximumPageY = 0;
                if (!TryQuantizeVirtualShadowPageRange(
                        lightBounds.GetMinimum().GetX(), lightBounds.GetMaximum().GetX(), level.pageWorldSize, minimumPageX, maximumPageX) ||
                    !TryQuantizeVirtualShadowPageRange(
                        lightBounds.GetMinimum().GetY(), lightBounds.GetMaximum().GetY(), level.pageWorldSize, minimumPageY, maximumPageY))
                {
                    continue;
                }

                minimumPageX = std::max(minimumPageX, -32768);
                minimumPageY = std::max(minimumPageY, -32768);
                maximumPageX = std::min(maximumPageX, 32767);
                maximumPageY = std::min(maximumPageY, 32767);
                for (Int32 pageY = minimumPageY; pageY <= maximumPageY; ++pageY)
                {
                    for (Int32 pageX = minimumPageX; pageX <= maximumPageX; ++pageX)
                    {
                        const VirtualShadowPageKey key = VirtualShadowPageKey::Create(pageX, pageY, levelIndex, level.depthEpoch);
                        if (key.IsValid())
                        {
                            keys.insert(key);
                            if (keys.size() > maximumInvalidatedKeys)
                            {
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
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

            Int32 workingMinimumX = 0;
            Int32 workingMinimumY = 0;
            Int32 workingMaximumX = 0;
            Int32 workingMaximumY = 0;
            Int32 boundsMinimumX = 0;
            Int32 boundsMinimumY = 0;
            Int32 boundsMaximumX = 0;
            Int32 boundsMaximumY = 0;
            if (!TryBuildVirtualShadowWorkingRegion(level.originPageX, workingMinimumX, workingMaximumX) ||
                !TryBuildVirtualShadowWorkingRegion(level.originPageY, workingMinimumY, workingMaximumY) ||
                !TryQuantizeVirtualShadowPageRange(
                    lightBounds.GetMinimum().GetX(), lightBounds.GetMaximum().GetX(), level.pageWorldSize, boundsMinimumX, boundsMaximumX) ||
                !TryQuantizeVirtualShadowPageRange(
                    lightBounds.GetMinimum().GetY(), lightBounds.GetMaximum().GetY(), level.pageWorldSize, boundsMinimumY, boundsMaximumY))
            {
                continue;
            }

            const Int32 minimumPageX = std::max(boundsMinimumX, workingMinimumX);
            const Int32 minimumPageY = std::max(boundsMinimumY, workingMinimumY);
            const Int32 maximumPageX = std::min(boundsMaximumX, workingMaximumX);
            const Int32 maximumPageY = std::min(boundsMaximumY, workingMaximumY);
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
        return Update(
            frameIndex, clipmaps, lightDirection, casters, VirtualShadowInvalidationCoverage::CurrentWorkingRegion, std::numeric_limits<SizeT>::max());
    }

    VirtualShadowInvalidationResult VirtualShadowInvalidationTracker::Update(UInt64 frameIndex,
                                                                             const VirtualShadowClipmapSet& clipmaps,
                                                                             Vector3 lightDirection,
                                                                             std::span<const VirtualShadowCasterSnapshot> casters,
                                                                             VirtualShadowInvalidationCoverage coverage,
                                                                             SizeT maximumInvalidatedKeys)
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
        bool keyCapacityAvailable = true;
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
                if (keyCapacityAvailable)
                {
                    keyCapacityAvailable = AppendInvalidationKeys(invalidatedKeys, clipmaps, caster.worldBounds, coverage, maximumInvalidatedKeys);
                }
                continue;
            }

            VirtualShadowTrackedCaster& tracked = iterator->second;
            if (tracked.revision != caster.revision)
            {
                if (keyCapacityAvailable)
                {
                    keyCapacityAvailable = AppendInvalidationKeys(invalidatedKeys, clipmaps, tracked.worldBounds, coverage, maximumInvalidatedKeys);
                }
                if (keyCapacityAvailable)
                {
                    keyCapacityAvailable = AppendInvalidationKeys(invalidatedKeys, clipmaps, caster.worldBounds, coverage, maximumInvalidatedKeys);
                }
                tracked.revision = caster.revision;
                tracked.worldBounds = caster.worldBounds;
            }
            tracked.lastSeenFrame = frameIndex;
        }

        for (auto iterator = trackedCasters_.begin(); iterator != trackedCasters_.end();)
        {
            if (iterator->second.lastSeenFrame != frameIndex)
            {
                if (keyCapacityAvailable)
                {
                    keyCapacityAvailable = AppendInvalidationKeys(invalidatedKeys, clipmaps, iterator->second.worldBounds, coverage, maximumInvalidatedKeys);
                }
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
