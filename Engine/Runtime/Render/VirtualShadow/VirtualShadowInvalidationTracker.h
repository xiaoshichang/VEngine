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

    struct VirtualShadowPageKeyBuildResult
    {
        std::vector<VirtualShadowPageKey> keys;
        bool exceededCapacity = false;
    };

    struct VirtualShadowSceneInvalidationResult
    {
        bool lightBasisChanged = false;
        std::vector<Aabb> changedBounds;
        std::vector<UInt64> changedCasterIDs;
    };

    [[nodiscard]] std::vector<VirtualShadowPageKey>
    BuildVirtualShadowPageKeysForBounds(UInt32 viewID, const VirtualShadowClipmapSet& clipmaps, const Aabb& worldBounds);
    [[nodiscard]] VirtualShadowPageKeyBuildResult BuildAbsoluteVirtualShadowPageKeysForBounds(
        UInt32 viewID, const VirtualShadowClipmapSet& clipmaps, const Aabb& worldBounds, SizeT maximumPageKeyCount);

    class VirtualShadowInvalidationTracker
    {
    public:
        [[nodiscard]] VirtualShadowSceneInvalidationResult
        UpdateScene(UInt64 frameIndex, Vector3 lightDirection, std::span<const VirtualShadowCasterSnapshot> casters);
        void Clear() noexcept;
        [[nodiscard]] UInt32 GetTrackedCasterCount() const noexcept;

    private:
        std::unordered_map<UInt64, VirtualShadowTrackedCaster> trackedCasters_;
        Vector3 lastLightDirection_ = Vector3::Zero();
        bool hasLightDirection_ = false;
    };
} // namespace ve
