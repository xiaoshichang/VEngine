#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestPageKeys()
    {
        const ve::VirtualShadowPageKey key = ve::VirtualShadowPageKey::Create(-123, 456, 2, -17);

        bool passed = true;
        passed &= Expect(key.GetPageX() == -123 && key.GetPageY() == 456, "Page XY should round-trip");
        passed &= Expect(key.GetClipmapLevel() == 2 && key.GetDepthEpoch() == -17, "Level and depth epoch should round-trip");
        passed &= Expect(!ve::VirtualShadowPageKey::Create(32768, 0, 0, 0).IsValid(), "Out-of-range X should be rejected");
        passed &= Expect(!ve::VirtualShadowPageKey::Create(0, 0, 4, 0).IsValid(), "Out-of-range clipmap level should be rejected");

        return passed;
    }

    bool TestClipmapQuantization()
    {
        const ve::Vector3 lightDirection = ve::Vector3::UnitZ();
        const ve::VirtualShadowClipmapSet first =
            ve::BuildVirtualShadowClipmaps(ve::Matrix44::Translation(ve::Vector3(10.0f, 0.0f, 5.0f)), lightDirection, 200.0f);
        const ve::VirtualShadowClipmapSet subPage =
            ve::BuildVirtualShadowClipmaps(ve::Matrix44::Translation(ve::Vector3(10.1f, 0.0f, 5.0f)), lightDirection, 200.0f);
        const ve::VirtualShadowClipmapSet crossedPage =
            ve::BuildVirtualShadowClipmaps(ve::Matrix44::Translation(ve::Vector3(10.5f, 0.0f, 5.0f)), lightDirection, 200.0f);
        const ve::VirtualShadowClipmapSet crossedDepth =
            ve::BuildVirtualShadowClipmaps(ve::Matrix44::Translation(ve::Vector3(10.0f, 0.0f, 8.2f)), lightDirection, 200.0f);

        bool passed = true;
        passed &= Expect(first.levels[0].originPageX == subPage.levels[0].originPageX, "Sub-page motion should preserve origin");
        passed &= Expect(crossedPage.levels[0].originPageX == first.levels[0].originPageX + 1, "Crossing a page should advance the finest origin once");
        passed &= Expect(crossedDepth.levels[0].depthEpoch == first.levels[0].depthEpoch + 1, "Crossing the depth step should advance depth epoch");
        passed &= Expect(first.levels[0].worldRadius == 25.0f && first.levels[1].worldRadius == 50.0f && first.levels[2].worldRadius == 100.0f,
                         "The first three clipmap levels should use D/8, D/4, and D/2 radii");
        passed &= Expect(first.levels[3].worldRadius == 200.0f, "Last clipmap should cover the shadow distance");
        passed &= Expect(!ve::BuildVirtualShadowClipmaps(ve::Matrix44::Identity(), lightDirection, ve::Math::DefaultEpsilon * 0.5f).valid,
                         "A shadow distance too small for stable page quantization should be rejected");
        passed &= Expect(!ve::BuildVirtualShadowClipmaps(ve::Matrix44::Identity(), lightDirection, std::numeric_limits<ve::Float32>::max()).valid,
                         "A shadow distance that overflows derived clipmap values should be rejected");
        passed &= Expect(
            !ve::BuildVirtualShadowClipmaps(ve::Matrix44::Translation(ve::Vector3(std::numeric_limits<ve::Float32>::max(), 0.0f, 0.0f)), lightDirection, 200.0f)
                 .valid,
            "A camera origin beyond checked page coordinates should be rejected");

        const ve::Float32 finestPageWorldSize = first.levels[0].pageWorldSize;
        const ve::VirtualShadowClipmapSet lastRepresentableRegion =
            ve::BuildVirtualShadowClipmaps(ve::Matrix44::Translation(ve::Vector3(32704.0f * finestPageWorldSize, 0.0f, 0.0f)), lightDirection, 200.0f);
        const ve::VirtualShadowClipmapSet overflowingRegion =
            ve::BuildVirtualShadowClipmaps(ve::Matrix44::Translation(ve::Vector3(32705.0f * finestPageWorldSize, 0.0f, 0.0f)), lightDirection, 200.0f);
        passed &= Expect(lastRepresentableRegion.valid, "A full working region ending at signed-16 maximum should remain valid");
        passed &= Expect(!overflowingRegion.valid, "A working region exceeding signed-16 page coordinates should be rejected");

        const ve::Float32 depthStep = 200.0f / 64.0f;
        const ve::VirtualShadowClipmapSet overflowingDepth =
            ve::BuildVirtualShadowClipmaps(ve::Matrix44::Translation(ve::Vector3(0.0f, 0.0f, 8388608.0f * depthStep)), lightDirection, 200.0f);
        passed &= Expect(!overflowingDepth.valid, "A depth epoch exceeding signed 24-bit packing should be rejected");
        return passed;
    }

    bool ContainsKey(std::span<const ve::VirtualShadowPageKey> keys, ve::VirtualShadowPageKey expected)
    {
        return std::ranges::find(keys, expected) != keys.end();
    }

    bool ContainsGpuInvalidationIdentity(std::span<const ve::VirtualShadowPageKey> keys, ve::VirtualShadowPageKey expected)
    {
        return std::ranges::any_of(
            keys, [expected](ve::VirtualShadowPageKey key) { return key.key0 == expected.key0 && key.GetClipmapLevel() == expected.GetClipmapLevel(); });
    }

    bool HasKeyNotIn(std::span<const ve::VirtualShadowPageKey> left, std::span<const ve::VirtualShadowPageKey> right)
    {
        return std::ranges::any_of(left, [right](ve::VirtualShadowPageKey key) { return !ContainsKey(right, key); });
    }

    bool TestCasterInvalidationHistory()
    {
        const ve::Vector3 firstDirection = ve::Vector3::UnitZ();
        const ve::VirtualShadowClipmapSet clipmaps = ve::BuildVirtualShadowClipmaps(ve::Matrix44::Identity(), firstDirection, 200.0f);
        const ve::Aabb a = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3(0.1f, 0.1f, 0.1f));
        const ve::Aabb b = ve::Aabb::FromCenterExtents(ve::Vector3(5.0f, 0.0f, 5.0f), ve::Vector3(0.1f, 0.1f, 0.1f));
        const std::vector<ve::VirtualShadowPageKey> aKeys = ve::BuildVirtualShadowPageKeysForBounds(clipmaps, a);
        const std::vector<ve::VirtualShadowPageKey> bKeys = ve::BuildVirtualShadowPageKeysForBounds(clipmaps, b);

        ve::VirtualShadowInvalidationTracker tracker;
        const ve::VirtualShadowCasterSnapshot atA[] = {{7, 1, a, true}};
        const ve::VirtualShadowInvalidationResult added = tracker.Update(1, clipmaps, firstDirection, atA);
        const ve::VirtualShadowCasterSnapshot atB[] = {{7, 2, b, true}};
        const ve::VirtualShadowInvalidationResult moved = tracker.Update(2, clipmaps, firstDirection, atB);
        const ve::VirtualShadowInvalidationResult removed = tracker.Update(3, clipmaps, firstDirection, {});

        bool passed = true;
        passed &= Expect(!aKeys.empty() && !bKeys.empty(), "Caster bounds should overlap clipmap pages");
        passed &= Expect(HasKeyNotIn(aKeys, bKeys) && HasKeyNotIn(bKeys, aKeys), "Separated caster bounds should have distinct page coverage");
        passed &= Expect(std::ranges::all_of(aKeys, [&added](ve::VirtualShadowPageKey key) { return ContainsKey(added.invalidatedKeys, key); }),
                         "Adding a caster should invalidate its new bounds");
        passed &= Expect(std::ranges::all_of(aKeys, [&moved](ve::VirtualShadowPageKey key) { return ContainsKey(moved.invalidatedKeys, key); }) &&
                             std::ranges::all_of(bKeys, [&moved](ve::VirtualShadowPageKey key) { return ContainsKey(moved.invalidatedKeys, key); }),
                         "Moving a caster should invalidate old and new bounds");
        passed &= Expect(std::ranges::all_of(bKeys, [&removed](ve::VirtualShadowPageKey key) { return ContainsKey(removed.invalidatedKeys, key); }),
                         "Removing a caster should invalidate its saved bounds");

        const ve::VirtualShadowClipmapSet changedClipmaps = ve::BuildVirtualShadowClipmaps(ve::Matrix44::Identity(), ve::Vector3::UnitX(), 200.0f);
        const ve::VirtualShadowInvalidationResult directionChanged = tracker.Update(4, changedClipmaps, ve::Vector3::UnitX(), {});
        passed &= Expect(directionChanged.fullInvalidation, "Changing light direction should request full invalidation");

        ve::VirtualShadowInvalidationTracker firstTracker;
        ve::VirtualShadowInvalidationTracker secondTracker;
        (void)firstTracker.Update(1, clipmaps, firstDirection, atA);
        (void)secondTracker.Update(1, clipmaps, firstDirection, atA);
        (void)firstTracker.Update(2, clipmaps, firstDirection, {});
        passed &= Expect(firstTracker.GetTrackedCasterCount() == 0 && secondTracker.GetTrackedCasterCount() == 1,
                         "Consuming one tracker should not alter another view's history");
        return passed;
    }

    bool TestGpuOnlyPrepareFrameEntryPoint()
    {
        ve::VirtualShadowViewCache viewCache(520);
        const ve::Aabb bounds = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3::One());
        const ve::VirtualShadowSceneItem item = {42, 1, bounds, true, true};
        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.screenWidth = 1920;
        input.screenHeight = 1080;
        input.cameraLocalToWorld = ve::Matrix44::Identity();
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.items = std::span<const ve::VirtualShadowSceneItem>(&item, 1);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f};

        const ve::VirtualShadowFramePacket packet = viewCache.PrepareFrame(input);
        bool passed = true;
        passed &= Expect(packet.valid && packet.enabled, "PrepareFrame should expose the GPU-driven VSM path");
        passed &= Expect(!packet.invalidatedPageKeys.empty(), "GPU-only frame preparation should preserve GPU invalidation payloads");
        return passed;
    }

    bool TestGpuViewCacheLocalInvalidation()
    {
        ve::VirtualShadowViewCache viewCache(520);
        const ve::Aabb firstBounds = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::Aabb movedBounds = ve::Aabb::FromCenterExtents(ve::Vector3(8.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::VirtualShadowSceneItem firstItem = {77, 1, firstBounds, true, true};

        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.cameraLocalToWorld = ve::Matrix44::Identity();
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f};
        input.items = std::span<const ve::VirtualShadowSceneItem>(&firstItem, 1);

        const ve::VirtualShadowFramePacket firstPacket = viewCache.PrepareFrame(input);
        (void)viewCache.ConsumeGpuCacheReset();

        const ve::VirtualShadowSceneItem movedItem = {77, 2, movedBounds, true, true};
        input.frameIndex = 2;
        input.items = std::span<const ve::VirtualShadowSceneItem>(&movedItem, 1);
        const ve::VirtualShadowFramePacket movedPacket = viewCache.PrepareFrame(input);
        const std::vector<ve::VirtualShadowPageKey> firstKeys = ve::BuildVirtualShadowPageKeysForBounds(movedPacket.clipmaps, firstBounds);
        const std::vector<ve::VirtualShadowPageKey> movedKeys = ve::BuildVirtualShadowPageKeysForBounds(movedPacket.clipmaps, movedBounds);

        bool passed = true;
        passed &= Expect(firstPacket.enabled && firstPacket.resetGpuCache, "The first GPU frame should initialize an enabled cache and request its pending resource reset");
        passed &= Expect(movedPacket.enabled && !movedPacket.resetGpuCache,
                         "Moving one caster should preserve compatible GPU page mappings");
        passed &= Expect(
            std::ranges::all_of(firstKeys, [&movedPacket](ve::VirtualShadowPageKey key) { return ContainsKey(movedPacket.invalidatedPageKeys, key); }) &&
                std::ranges::all_of(movedKeys, [&movedPacket](ve::VirtualShadowPageKey key) { return ContainsKey(movedPacket.invalidatedPageKeys, key); }),
            "A moved GPU caster should invalidate pages overlapped by both old and new bounds");

        const ve::VirtualShadowGpuConstants movedConstants = ve::BuildVirtualShadowGpuConstants(movedPacket);
        passed &= Expect(movedConstants.invalidationCount == movedPacket.invalidatedPageKeys.size(),
                         "GPU constants should expose the number of compact invalidation keys");
        for (ve::SizeT keyIndex = 0; keyIndex < movedPacket.invalidatedPageKeys.size(); ++keyIndex)
        {
            passed &= Expect(movedConstants.invalidationEntries[keyIndex].key0 == movedPacket.invalidatedPageKeys[keyIndex].key0 &&
                                 movedConstants.invalidationEntries[keyIndex].key1 == movedPacket.invalidatedPageKeys[keyIndex].key1,
                             "GPU constants should preserve each invalidated absolute page key");
        }

        ve::VirtualShadowFramePacket fullContentPacket = movedPacket;
        fullContentPacket.invalidateAllGpuPages = true;
        const ve::VirtualShadowGpuConstants fullContentConstants = ve::BuildVirtualShadowGpuConstants(fullContentPacket);
        passed &= Expect(fullContentConstants.invalidationCount == ve::InvalidVirtualShadowGpuInvalidationCount,
                         "GPU constants should encode full resident-content invalidation with the sentinel count");

        input.frameIndex = 3;
        input.light.direction = ve::Vector3::UnitX();
        const ve::VirtualShadowFramePacket directionPacket = viewCache.PrepareFrame(input);
        passed &= Expect(directionPacket.resetGpuCache, "Changing the directional-light basis should reset GPU page mappings");
        return passed;
    }

    bool TestGpuViewCacheDormantPageInvalidation()
    {
        ve::VirtualShadowViewCache viewCache(520);
        const ve::Aabb firstBounds = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::Aabb movedBounds = ve::Aabb::FromCenterExtents(ve::Vector3(8.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::VirtualShadowSceneItem firstItem = {91, 1, firstBounds, true, true};

        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.cameraLocalToWorld = ve::Matrix44::Identity();
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f};
        input.items = std::span<const ve::VirtualShadowSceneItem>(&firstItem, 1);
        const ve::VirtualShadowFramePacket firstPacket = viewCache.PrepareFrame(input);
        const std::vector<ve::VirtualShadowPageKey> originalKeys = ve::BuildVirtualShadowPageKeysForBounds(firstPacket.clipmaps, firstBounds);
        (void)viewCache.ConsumeGpuCacheReset();

        const ve::VirtualShadowSceneItem movedItem = {91, 2, movedBounds, true, true};
        input.frameIndex = 2;
        input.cameraLocalToWorld = ve::Matrix44::Translation(ve::Vector3(10000.0f, 10000.0f, 10000.0f));
        input.items = std::span<const ve::VirtualShadowSceneItem>(&movedItem, 1);
        const ve::VirtualShadowFramePacket movedWhileDormant = viewCache.PrepareFrame(input);

        bool passed = true;
        passed &= Expect(!originalKeys.empty(), "The initial caster should cover resident-capable pages");
        passed &= Expect(!movedWhileDormant.resetGpuCache, "Camera movement should preserve compatible absolute page mappings");
        passed &= Expect(std::ranges::all_of(originalKeys,
                                             [&movedWhileDormant](ve::VirtualShadowPageKey key)
                                             { return ContainsGpuInvalidationIdentity(movedWhileDormant.invalidatedPageKeys, key); }),
                         "Moving a caster outside the current working region should still invalidate dormant pages by absolute XY and clipmap level");
        return passed;
    }

    bool TestGpuViewCacheDefersUnsubmittedState()
    {
        const ve::Aabb firstBounds = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::Aabb movedBounds = ve::Aabb::FromCenterExtents(ve::Vector3(8.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::VirtualShadowSceneItem firstItem = {103, 1, firstBounds, true, true};
        const ve::VirtualShadowSceneItem movedItem = {103, 2, movedBounds, true, true};

        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.cameraLocalToWorld = ve::Matrix44::Identity();
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f};
        input.items = std::span<const ve::VirtualShadowSceneItem>(&firstItem, 1);

        ve::VirtualShadowViewCache disabledCache(520);
        (void)disabledCache.PrepareFrame(input);
        (void)disabledCache.ConsumeGpuCacheReset();
        input.frameIndex = 2;
        input.light.enabled = false;
        input.light.shadowDistance = 400.0f;
        const ve::VirtualShadowFramePacket disabledPacket = disabledCache.PrepareFrame(input);
        input.frameIndex = 3;
        input.light.enabled = true;
        const ve::VirtualShadowFramePacket reenabledPacket = disabledCache.PrepareFrame(input);

        ve::VirtualShadowViewCache invalidProjectionCache(520);
        input.frameIndex = 1;
        input.light.shadowDistance = 200.0f;
        input.items = std::span<const ve::VirtualShadowSceneItem>(&firstItem, 1);
        (void)invalidProjectionCache.PrepareFrame(input);
        (void)invalidProjectionCache.ConsumeGpuCacheReset();
        input.frameIndex = 2;
        input.viewProjection = ve::Matrix44::Zero();
        input.items = std::span<const ve::VirtualShadowSceneItem>(&movedItem, 1);
        const ve::VirtualShadowFramePacket invalidProjectionPacket = invalidProjectionCache.PrepareFrame(input);
        input.frameIndex = 3;
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        const ve::VirtualShadowFramePacket recoveredPacket = invalidProjectionCache.PrepareFrame(input);

        bool passed = true;
        passed &= Expect(!disabledPacket.enabled && reenabledPacket.resetGpuCache,
                         "A shadow-distance change while disabled should reset mappings on the next enabled GPU frame");
        passed &= Expect(!invalidProjectionPacket.enabled && !recoveredPacket.invalidatedPageKeys.empty(),
                         "An invalid projection frame should not consume caster changes before a GPU clear pass can receive them");
        return passed;
    }

    bool TestVirtualShadowNormalizedPageGutter()
    {
        constexpr ve::Float32 ExpectedGutter =
            static_cast<ve::Float32>(ve::VirtualShadowPageGutter) / static_cast<ve::Float32>(ve::VirtualShadowPhysicalPageContentSize);
        bool passed = true;
        passed &= Expect(ve::NearlyEqual(ve::VirtualShadowNormalizedPageGutter, ExpectedGutter),
                         "Virtual-shadow page clipping should use one physical gutter texel in normalized page coordinates");
        passed &= Expect(ve::VirtualShadowNormalizedPageGutter > 0.0f && ve::VirtualShadowNormalizedPageGutter < 1.0f,
                         "Virtual-shadow normalized page gutter should be a finite fraction of one logical page");
        passed &= Expect(ve::GetVirtualShadowPhysicalPageCapacity(4096) == 1024 && ve::GetVirtualShadowPhysicalPageCapacity(2048) == 256,
                         "GPU atlas capacity should use 128-pixel physical slots");
        return passed;
    }

    bool TestVirtualShadowPageClearGeometry()
    {
        return Expect(ve::VirtualShadowPageClearVertexCount == 6, "GPU physical-page clearing should use two exact triangles");
    }

    bool TestWorldDepthBiasConversion()
    {
        constexpr ve::Float32 WorldDepthBias = 0.001f;
        constexpr ve::Float32 DepthRange = 1024.0f;
        constexpr ve::Float32 ExpectedNormalizedBias = WorldDepthBias / DepthRange;

        bool passed = true;
        passed &= Expect(std::abs(ve::ConvertVirtualShadowWorldDepthBiasToNormalized(WorldDepthBias, DepthRange) - ExpectedNormalizedBias) < 1.0e-10f,
                         "World-space depth bias should be normalized by the shadow depth range");
        passed &= Expect(ve::ConvertVirtualShadowWorldDepthBiasToNormalized(-WorldDepthBias, DepthRange) == 0.0f,
                         "Negative world-space depth bias should be rejected");
        passed &=
            Expect(ve::ConvertVirtualShadowWorldDepthBiasToNormalized(WorldDepthBias, 0.0f) == 0.0f, "A non-positive shadow depth range should be rejected");
        passed &= Expect(ve::ConvertVirtualShadowWorldDepthBiasToNormalized(std::numeric_limits<ve::Float32>::infinity(), DepthRange) == 0.0f,
                         "A non-finite world-space depth bias should be rejected");
        return passed;
    }

} // namespace

int main()
{
    if (TestPageKeys() && TestClipmapQuantization() && TestCasterInvalidationHistory() && TestGpuOnlyPrepareFrameEntryPoint() &&
        TestGpuViewCacheLocalInvalidation() && TestGpuViewCacheDefersUnsubmittedState() && TestGpuViewCacheDormantPageInvalidation() &&
        TestVirtualShadowNormalizedPageGutter() && TestVirtualShadowPageClearGeometry() && TestWorldDepthBiasConversion())
    {
        std::cout << "VEngineVirtualShadowTests passed" << '\n';
        return 0;
    }

    return 1;
}
