#pragma once

#include "Engine/Runtime/Math/Bounds.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"

#include <span>
#include <unordered_map>
#include <vector>

namespace ve
{
    struct VirtualShadowCasterSnapshot
    {
        UInt64 renderItemID = 0;
        UInt64 revision = 0;
        Aabb worldBounds = Aabb(Vector3::Zero(), Vector3::Zero());
        bool castShadows = true;
    };

    struct VirtualShadowTrackedCaster
    {
        UInt64 revision = 0;
        Aabb worldBounds = Aabb(Vector3::Zero(), Vector3::Zero());
        UInt64 lastSeenFrame = 0;
    };

    struct VirtualShadowInvalidationResult
    {
        std::vector<VirtualShadowPageKey> invalidatedKeys;
        bool fullInvalidation = false;
    };

    enum class VirtualShadowInvalidationCoverage
    {
        CurrentWorkingRegion,
        AllAbsolutePages
    };

    [[nodiscard]] std::vector<VirtualShadowPageKey> BuildVirtualShadowPageKeysForBounds(const VirtualShadowClipmapSet& clipmaps, const Aabb& worldBounds);

    class VirtualShadowInvalidationTracker
    {
    public:
        [[nodiscard]] VirtualShadowInvalidationResult
        Update(UInt64 frameIndex, const VirtualShadowClipmapSet& clipmaps, Vector3 lightDirection, std::span<const VirtualShadowCasterSnapshot> casters);
        [[nodiscard]] VirtualShadowInvalidationResult Update(UInt64 frameIndex,
                                                             const VirtualShadowClipmapSet& clipmaps,
                                                             Vector3 lightDirection,
                                                             std::span<const VirtualShadowCasterSnapshot> casters,
                                                             VirtualShadowInvalidationCoverage coverage,
                                                             SizeT maximumInvalidatedKeys);
        void Clear() noexcept;
        [[nodiscard]] UInt32 GetTrackedCasterCount() const noexcept;

    private:
        std::unordered_map<UInt64, VirtualShadowTrackedCaster> trackedCasters_;
        Vector3 lastLightDirection_ = Vector3::Zero();
        bool hasLightDirection_ = false;
    };
} // namespace ve
