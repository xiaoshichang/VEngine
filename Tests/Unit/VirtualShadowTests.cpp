#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/RenderViewState.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include <unordered_set>
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

    bool TestPageKeysAndResidentTable()
    {
        const ve::VirtualShadowPageKey key = ve::VirtualShadowPageKey::Create(-123, 456, 2, -17);

        bool passed = true;
        passed &= Expect(key.GetPageX() == -123 && key.GetPageY() == 456, "Page XY should round-trip");
        passed &= Expect(key.GetClipmapLevel() == 2 && key.GetDepthEpoch() == -17, "Level and depth epoch should round-trip");
        passed &= Expect(!ve::VirtualShadowPageKey::Create(32768, 0, 0, 0).IsValid(), "Out-of-range X should be rejected");
        passed &= Expect(!ve::VirtualShadowPageKey::Create(0, 0, 4, 0).IsValid(), "Out-of-range clipmap level should be rejected");

        ve::VirtualShadowPageTable table;
        passed &= Expect(table.Insert(key, 19), "Resident mapping should insert");
        passed &= Expect(table.Find(key).value_or(ve::InvalidVirtualShadowPhysicalPage) == 19, "Resident mapping should resolve");
        passed &= Expect(!table.Find(ve::VirtualShadowPageKey::Create(9, 9, 0, 0)).has_value(), "Missing key should remain missing");
        const auto insertedEntry = std::ranges::find_if(
            table.GetGpuEntries(), [&key](const ve::VirtualShadowGpuPageEntry& entry) { return entry.key0 == key.key0 && entry.key1 == key.key1; });
        passed &= Expect(insertedEntry != table.GetGpuEntries().end() && (insertedEntry->flags & ve::VirtualShadowGpuPageEntryValid) != 0u,
                         "Inserted GPU mappings should set the valid flag");

        return passed;
    }

    std::vector<ve::VirtualShadowPageKey> FindCollidingKeys(ve::UInt32 count)
    {
        std::vector<ve::VirtualShadowPageKey> keys;
        keys.reserve(count);
        ve::UInt32 targetBucket = 0;

        for (ve::Int32 pageX = -32768; pageX <= 32767 && keys.size() < count; ++pageX)
        {
            const ve::VirtualShadowPageKey candidate = ve::VirtualShadowPageKey::Create(pageX, 37, 1, 5);
            const ve::UInt32 bucket = ve::HashVirtualShadowPageKey(candidate) & (ve::VirtualShadowPageTableCapacity - 1u);
            if (keys.empty())
            {
                targetBucket = bucket;
            }
            if (bucket == targetBucket)
            {
                keys.push_back(candidate);
            }
        }

        return keys;
    }

    bool TestResidentTableBoundsProbes()
    {
        bool passed = true;

        ve::VirtualShadowPageTable loadedTable;
        for (ve::UInt32 index = 0; index < 1024; ++index)
        {
            const ve::VirtualShadowPageKey key =
                ve::VirtualShadowPageKey::Create(static_cast<ve::Int32>(index), static_cast<ve::Int32>(index / 64), index % 4, 0);
            passed &= Expect(loadedTable.Insert(key, index), "A half-full resident table should accept deterministic entries");
        }
        for (ve::UInt32 index = 0; index < 1024; ++index)
        {
            const ve::VirtualShadowPageKey key =
                ve::VirtualShadowPageKey::Create(static_cast<ve::Int32>(index), static_cast<ve::Int32>(index / 64), index % 4, 0);
            passed &=
                Expect(loadedTable.Find(key).value_or(ve::InvalidVirtualShadowPhysicalPage) == index, "A half-full resident table should resolve every entry");
        }

        const std::vector<ve::VirtualShadowPageKey> collidingKeys = FindCollidingKeys(ve::VirtualShadowPageTableMaxProbes + 1u);
        passed &= Expect(collidingKeys.size() == ve::VirtualShadowPageTableMaxProbes + 1u, "Collision fixture should find seventeen keys in one bucket");

        ve::VirtualShadowPageTable collisionTable;
        for (ve::UInt32 index = 0; index < ve::VirtualShadowPageTableMaxProbes; ++index)
        {
            passed &= Expect(collisionTable.Insert(collidingKeys[index], index), "The first sixteen colliding mappings should insert");
        }
        passed &= Expect(!collisionTable.Insert(collidingKeys.back(), 99), "The seventeenth colliding mapping should exceed the probe limit");
        passed &= Expect(!collisionTable.Find(collidingKeys.back()).has_value(), "A key beyond the probe limit should remain missing");

        return passed;
    }

    ve::VirtualShadowPageKey MakeKey(ve::Int32 pageX)
    {
        return ve::VirtualShadowPageKey::Create(pageX, 0, 0, 0);
    }

    bool TestPhysicalPageCacheLifecycle()
    {
        ve::VirtualShadowPageCache cache(2);
        cache.BeginFrame(10);
        const auto a = cache.Request(ve::VirtualShadowPageRequest{MakeKey(0), 100});
        const auto b = cache.Request(ve::VirtualShadowPageRequest{MakeKey(1), 90});

        bool passed = true;
        passed &= Expect(a.has_value() && b.has_value(), "Two free physical pages should allocate");
        passed &= Expect(cache.GetDirtyPageCount() == 2, "New physical pages should start dirty");
        passed &= Expect(cache.BuildResidentPageTable().GetSize() == 0, "Dirty pages should not be exposed as resident");

        const ve::VirtualShadowPageKey renderedKeys[] = {MakeKey(0), MakeKey(1)};
        cache.MarkRendered(renderedKeys);
        passed &= Expect(cache.GetDirtyPageCount() == 0, "Rendered pages should become clean");
        passed &= Expect(cache.BuildResidentPageTable().GetSize() == 2, "Clean pages should be exposed as resident");

        cache.BeginFrame(11);
        passed &= Expect(cache.Request(ve::VirtualShadowPageRequest{MakeKey(0), 100}) == a, "A cache hit should preserve its physical slot");
        const auto c = cache.Request(ve::VirtualShadowPageRequest{MakeKey(2), 80});
        passed &= Expect(c.has_value(), "Unpinned page should be evicted");
        passed &= Expect(cache.Contains(MakeKey(0)), "Pinned page should survive eviction");
        passed &= Expect(!cache.Contains(MakeKey(1)), "Oldest unpinned page should be evicted");
        passed &= Expect(cache.Contains(MakeKey(2)), "Replacement page should become cached");

        return passed;
    }

    bool TestPageCachePressurePriorityAndIsolation()
    {
        bool passed = true;

        ve::VirtualShadowPageCache pressureCache(2);
        pressureCache.BeginFrame(1);
        const std::vector<ve::VirtualShadowPageRequest> requests = {
            {MakeKey(0), 10},
            {MakeKey(1), 40},
            {MakeKey(2), 30},
            {MakeKey(3), 20},
        };
        const ve::VirtualShadowRequestResolution resolution = pressureCache.ResolveRequests(requests);
        passed &= Expect(resolution.allocated == 2 && resolution.missing == 2, "Over-capacity requests should degrade to two allocations and two misses");
        passed &= Expect(pressureCache.Contains(MakeKey(1)) && pressureCache.Contains(MakeKey(2)), "Higher-priority requests should allocate first");
        const ve::UInt32 dirtyBeforeHistoryClear = pressureCache.GetDirtyPageCount();
        pressureCache.ClearRequestHistory();
        passed &= Expect(pressureCache.GetRequestHistorySize() == 0 && pressureCache.GetDirtyPageCount() == dirtyBeforeHistoryClear,
                         "Clearing request history should not dirty or discard physical pages");

        ve::VirtualShadowPageCache firstCache(2);
        ve::VirtualShadowPageCache secondCache(2);
        firstCache.BeginFrame(2);
        secondCache.BeginFrame(2);
        const ve::VirtualShadowPageKey firstKey = MakeKey(11);
        const ve::VirtualShadowPageKey secondKey = MakeKey(22);
        passed &= Expect(firstCache.Request({firstKey, 1}).has_value(), "The first isolated cache should allocate");
        passed &= Expect(secondCache.Request({secondKey, 1}).has_value(), "The second isolated cache should allocate");
        firstCache.MarkRendered(std::span<const ve::VirtualShadowPageKey>(&firstKey, 1));
        secondCache.MarkRendered(std::span<const ve::VirtualShadowPageKey>(&secondKey, 1));
        firstCache.InvalidateAll();
        passed &= Expect(firstCache.GetDirtyPageCount() == 1, "Invalidating one cache should dirty its page");
        passed &= Expect(secondCache.GetDirtyPageCount() == 0 && secondCache.Contains(secondKey), "A second cache should keep independent mappings and state");

        const ve::VirtualShadowPhysicalPageOrigin firstOrigin = ve::GetVirtualShadowPhysicalPageOrigin(0, 520);
        const ve::VirtualShadowPhysicalPageOrigin secondOrigin = ve::GetVirtualShadowPhysicalPageOrigin(1, 520);
        passed &= Expect(firstOrigin.x == 1 && firstOrigin.y == 1, "Physical page interiors should begin after the gutter");
        passed &= Expect(secondOrigin.x == 129 && secondOrigin.y == 1, "A 128-pixel physical slot should contain both gutters");
        passed &= Expect(ve::GetVirtualShadowPhysicalPageCapacity(4096) == 1024 && ve::GetVirtualShadowPhysicalPageCapacity(2048) == 256,
                         "Committed atlas capacity should use 128-pixel physical slots");

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

    bool RequestsAreUniqueAndValid(std::span<const ve::VirtualShadowPageRequest> requests)
    {
        std::unordered_set<ve::VirtualShadowPageKey, ve::VirtualShadowPageKeyHash> keys;
        for (const ve::VirtualShadowPageRequest& request : requests)
        {
            if (request.key.GetClipmapLevel() >= ve::VirtualShadowClipmapLevelCount || !keys.insert(request.key).second)
            {
                return false;
            }
        }
        return true;
    }

    bool TestReceiverRequests()
    {
        const ve::Matrix44 cameraLocalToWorld = ve::Matrix44::Identity();
        const ve::VirtualShadowClipmapSet clipmaps = ve::BuildVirtualShadowClipmaps(cameraLocalToWorld, -ve::Vector3::UnitY(), 200.0f);
        const ve::VirtualShadowReceiver receivers[] = {
            {1, ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3::One()), true},
            {2, ve::Aabb::FromCenterExtents(ve::Vector3(1000.0f, 0.0f, 5.0f), ve::Vector3::One()), true},
            {3, ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3::One()), true},
        };
        const ve::VirtualShadowRequestBuildInput perspectiveInput{
            ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f), cameraLocalToWorld, clipmaps, receivers};
        const ve::VirtualShadowRequestBuildInput orthographicInput{
            ve::BuildOrthographicProjection(10.0f, 1.0f, 0.1f, 100.0f), cameraLocalToWorld, clipmaps, receivers};

        const std::vector<ve::VirtualShadowPageRequest> perspectiveRequests = ve::BuildVirtualShadowPageRequests(perspectiveInput);
        const std::vector<ve::VirtualShadowPageRequest> orthographicRequests = ve::BuildVirtualShadowPageRequests(orthographicInput);

        bool passed = true;
        passed &= Expect(!perspectiveRequests.empty(), "A visible perspective receiver should request pages");
        passed &= Expect(!orthographicRequests.empty(), "A visible orthographic receiver should request pages");
        passed &= Expect(RequestsAreUniqueAndValid(perspectiveRequests), "Perspective requests should be deduplicated and use levels zero through three");
        passed &= Expect(RequestsAreUniqueAndValid(orthographicRequests), "Orthographic requests should be deduplicated and use levels zero through three");

        const ve::VirtualShadowReceiver disabledReceiver = {4, ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3::One()), false};
        ve::VirtualShadowRequestBuildInput disabledInput = perspectiveInput;
        disabledInput.receivers = std::span<const ve::VirtualShadowReceiver>(&disabledReceiver, 1);
        passed &= Expect(ve::BuildVirtualShadowPageRequests(disabledInput).empty(), "A receiver with shadows disabled should not request pages");

        const ve::VirtualShadowClipmapSet slicedClipmaps = ve::BuildVirtualShadowClipmaps(cameraLocalToWorld, -ve::Vector3::UnitY(), 80.0f);
        for (ve::UInt32 expectedLevel = 0; expectedLevel < ve::VirtualShadowClipmapLevelCount; ++expectedLevel)
        {
            constexpr ve::Float32 ReceiverDepths[] = {5.0f, 15.0f, 30.0f, 60.0f};
            const ve::VirtualShadowReceiver slicedReceiver = {
                10 + expectedLevel,
                ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, ReceiverDepths[expectedLevel]), ve::Vector3(0.25f, 0.25f, 0.25f)),
                true};
            const ve::VirtualShadowRequestBuildInput slicedInput{ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f),
                                                                 cameraLocalToWorld,
                                                                 slicedClipmaps,
                                                                 std::span<const ve::VirtualShadowReceiver>(&slicedReceiver, 1)};
            const std::vector<ve::VirtualShadowPageRequest> slicedRequests = ve::BuildVirtualShadowPageRequests(slicedInput);
            passed &= Expect(!slicedRequests.empty(), "A receiver inside a clipmap camera-depth slice should request pages");
            passed &= Expect(std::ranges::all_of(slicedRequests,
                                                 [expectedLevel](const ve::VirtualShadowPageRequest& request)
                                                 { return request.key.GetClipmapLevel() == expectedLevel; }),
                             "A receiver should request only clipmap levels whose camera-depth slices overlap it");
        }

        const ve::VirtualShadowClipmapSet xyClipmaps = ve::BuildVirtualShadowClipmaps(cameraLocalToWorld, ve::Vector3::UnitZ(), 80.0f);
        const ve::VirtualShadowReceiver wideReceiver = {
            20, ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 15.0f), ve::Vector3(19.0f, 0.25f, 0.25f)), true};
        const ve::VirtualShadowRequestBuildInput widePerspectiveInput{ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f),
                                                                      cameraLocalToWorld,
                                                                      xyClipmaps,
                                                                      std::span<const ve::VirtualShadowReceiver>(&wideReceiver, 1)};
        const ve::VirtualShadowRequestBuildInput wideOrthographicInput{ve::BuildOrthographicProjection(10.0f, 1.0f, 0.1f, 100.0f),
                                                                       cameraLocalToWorld,
                                                                       xyClipmaps,
                                                                       std::span<const ve::VirtualShadowReceiver>(&wideReceiver, 1)};
        const std::vector<ve::VirtualShadowPageRequest> widePerspectiveRequests = ve::BuildVirtualShadowPageRequests(widePerspectiveInput);
        const std::vector<ve::VirtualShadowPageRequest> wideOrthographicRequests = ve::BuildVirtualShadowPageRequests(wideOrthographicInput);
        passed &=
            Expect(!widePerspectiveRequests.empty() &&
                       std::ranges::all_of(widePerspectiveRequests,
                                           [](const ve::VirtualShadowPageRequest& request)
                                           { return request.key.GetClipmapLevel() == 1 && request.key.GetPageX() >= -37 && request.key.GetPageX() <= 36; }),
                   "Perspective requests should clip receiver XY coverage to the selected camera frustum slice");
        passed &=
            Expect(!wideOrthographicRequests.empty() &&
                       std::ranges::all_of(wideOrthographicRequests,
                                           [](const ve::VirtualShadowPageRequest& request)
                                           { return request.key.GetClipmapLevel() == 1 && request.key.GetPageX() >= -16 && request.key.GetPageX() <= 16; }),
                   "Orthographic requests should clip receiver XY coverage to the selected camera frustum slice");

        const ve::VirtualShadowReceiver enormousReceiver = {
            21, ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 15.0f), ve::Vector3(1.0e20f, 0.25f, 0.25f)), true};
        ve::Int32 quantizedCoordinate = 0;
        passed &= Expect(!ve::TryQuantizeVirtualShadowCoordinate(1.0e20f, 1.0f, quantizedCoordinate),
                         "Page coordinates outside Int32 range should be rejected by checked quantization");
        passed &= Expect(ve::BuildVirtualShadowPageKeysForBounds(xyClipmaps, enormousReceiver.worldBounds).empty(),
                         "Invalidation page coordinates outside checked Int32 range should be rejected without undefined conversion");

        ve::VirtualShadowClipmapSet unsafeClipmaps = xyClipmaps;
        for (ve::VirtualShadowClipmapLevel& level : unsafeClipmaps.levels)
        {
            level.originPageX = std::numeric_limits<ve::Int32>::max();
        }
        ve::VirtualShadowRequestBuildInput unsafeInput = widePerspectiveInput;
        unsafeInput.clipmaps = unsafeClipmaps;
        passed &= Expect(ve::BuildVirtualShadowPageRequests(unsafeInput).empty(),
                         "Request building should reject a working region that cannot fit signed-16 page coordinates");

        for (ve::VirtualShadowClipmapLevel& level : unsafeClipmaps.levels)
        {
            level.originPageX = 0;
            level.pageWorldSize = 0.0f;
        }
        unsafeInput.clipmaps = unsafeClipmaps;
        passed &= Expect(ve::BuildVirtualShadowPageRequests(unsafeInput).empty(), "Request building should reject non-positive page quantization steps");
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

    bool TestLargePageCacheIsolation()
    {
        ve::VirtualShadowPageCache sceneCache(256);
        ve::VirtualShadowPageCache gameCache(1024);
        sceneCache.BeginFrame(10);
        gameCache.BeginFrame(30);

        const ve::VirtualShadowPageKey sceneKey = ve::VirtualShadowPageKey::Create(-20, 5, 1, -2);
        const ve::VirtualShadowPageKey gameKey = ve::VirtualShadowPageKey::Create(40, -7, 3, 8);
        const auto sceneSlot = sceneCache.Request({sceneKey, 100});
        const auto gameSlot = gameCache.Request({gameKey, 200});
        sceneCache.MarkRendered(std::span<const ve::VirtualShadowPageKey>(&sceneKey, 1));
        gameCache.MarkRendered(std::span<const ve::VirtualShadowPageKey>(&gameKey, 1));
        sceneCache.InvalidateAll();

        bool passed = true;
        passed &= Expect(sceneCache.GetCapacity() == 256 && gameCache.GetCapacity() == 1024, "Views should preserve distinct physical capacities");
        passed &= Expect(sceneSlot.has_value() && gameSlot.has_value(), "Both large caches should allocate independently");
        passed &= Expect(sceneCache.Contains(sceneKey) && !sceneCache.Contains(gameKey), "Scene cache should contain only its absolute key sequence");
        passed &= Expect(gameCache.Contains(gameKey) && !gameCache.Contains(sceneKey), "Game cache should contain only its absolute key sequence");
        passed &= Expect(sceneCache.GetDirtyPageCount() == 1 && gameCache.GetDirtyPageCount() == 0,
                         "Invalidation and clean-state statistics should remain independent");
        return passed;
    }

    bool TestCpuViewCachePacketAndOverlap()
    {
        ve::VirtualShadowViewCache viewCache(520);
        const ve::Aabb bounds = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3::One());
        const ve::VirtualShadowSceneItem items[] = {
            {42, 1, bounds, true, true, true, nullptr},
            {43, 1, bounds, false, true, true, nullptr},
        };
        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.cameraCutRevision = 0;
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.items = items;
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f, 1};

        const ve::VirtualShadowFramePacket packet = viewCache.PrepareFrame(input);
        bool passed = true;
        bool foundCaster = false;
        for (const ve::VirtualShadowDirtyPageDraw& draw : packet.dirtyPages)
        {
            foundCaster |= std::ranges::find(draw.casterRenderItemIDs, 42u) != draw.casterRenderItemIDs.end();
            passed &= Expect(std::ranges::find(draw.casterRenderItemIDs, 43u) == draw.casterRenderItemIDs.end(),
                             "Transparent render items should not enter first-stage caster draw lists");
        }

        passed &= Expect(packet.valid && packet.enabled, "A suitable directional light should produce an enabled valid packet");
        passed &= Expect(packet.statistics.requested > 0 && !packet.dirtyPages.empty(), "The CPU view cache should request and prepare dirty pages");
        passed &= Expect(foundCaster, "A dirty page should include an overlapping opaque caster");

        std::vector<ve::VirtualShadowPageKey> renderedKeys;
        for (const ve::VirtualShadowDirtyPageDraw& draw : packet.dirtyPages)
        {
            renderedKeys.push_back(draw.key);
        }
        viewCache.MarkRendered(renderedKeys);

        input.frameIndex = 2;
        input.items = std::span<const ve::VirtualShadowSceneItem>(&items[0], 1);
        input.light.depthBias = 0.002f;
        input.light.normalBias = 0.1f;
        input.light.revision = 2;
        const ve::VirtualShadowFramePacket biasPacket = viewCache.PrepareFrame(input);
        passed &= Expect(biasPacket.statistics.dirty == 0, "Bias-only revisions should not dirty resident pages");
        const auto stableKey = renderedKeys.front();
        const auto stableSlot = viewCache.GetPageCache().FindPhysicalPage(stableKey);

        input.frameIndex = 3;
        input.cameraCutRevision = 1;
        const ve::VirtualShadowFramePacket cameraCutPacket = viewCache.PrepareFrame(input);
        passed &= Expect(cameraCutPacket.statistics.dirty == 0, "A camera cut should clear request history without dirtying stable resident pages");
        passed &= Expect(stableSlot.has_value() && viewCache.GetPageCache().FindPhysicalPage(stableKey) == stableSlot,
                         "A camera cut should preserve stable-key physical mappings");

        input.frameIndex = 4;
        input.light.shadowDistance = 220.0f;
        const ve::VirtualShadowFramePacket distancePacket = viewCache.PrepareFrame(input);
        passed &= Expect(distancePacket.statistics.dirty > 0, "Changing shadow distance should invalidate clean mappings with the old projection");

        renderedKeys.clear();
        for (const ve::VirtualShadowDirtyPageDraw& draw : distancePacket.dirtyPages)
        {
            renderedKeys.push_back(draw.key);
        }
        viewCache.MarkRendered(renderedKeys);

        input.frameIndex = 5;
        input.light.direction = ve::Vector3::UnitX();
        const ve::VirtualShadowFramePacket directionPacket = viewCache.PrepareFrame(input);
        passed &= Expect(directionPacket.statistics.dirty > 0, "A light-direction change should dirty resident pages");

        ve::VirtualShadowPrepareInput disabledInput = input;
        disabledInput.frameIndex = 6;
        disabledInput.light.enabled = false;
        const ve::VirtualShadowFramePacket disabledPacket = viewCache.PrepareFrame(disabledInput);
        passed &= Expect(disabledPacket.valid && !disabledPacket.enabled && disabledPacket.dirtyPages.empty(),
                         "No suitable light should return a disabled but valid frame packet");

        ve::VirtualShadowPrepareInput invalidInput = input;
        invalidInput.frameIndex = 7;
        invalidInput.light.shadowDistance = ve::Math::DefaultEpsilon * 0.5f;
        const ve::VirtualShadowFramePacket invalidPacket = viewCache.PrepareFrame(invalidInput);
        passed &= Expect(!invalidPacket.valid && !invalidPacket.enabled, "Unsafe clipmap quantization should return a disabled invalid frame packet");
        return passed;
    }

    bool TestGpuViewCacheLocalInvalidation()
    {
        ve::VirtualShadowViewCache viewCache(520);
        const ve::Aabb firstBounds = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::Aabb movedBounds = ve::Aabb::FromCenterExtents(ve::Vector3(8.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::VirtualShadowSceneItem firstItem = {77, 1, firstBounds, true, true, true, nullptr};

        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.cameraCutRevision = 0;
        input.cameraLocalToWorld = ve::Matrix44::Identity();
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f, 1};
        input.items = std::span<const ve::VirtualShadowSceneItem>(&firstItem, 1);

        const ve::VirtualShadowFramePacket firstPacket = viewCache.PrepareGpuFrame(input);
        (void)viewCache.ConsumeGpuCacheReset();

        const ve::VirtualShadowSceneItem movedItem = {77, 2, movedBounds, true, true, true, nullptr};
        input.frameIndex = 2;
        input.items = std::span<const ve::VirtualShadowSceneItem>(&movedItem, 1);
        const ve::VirtualShadowFramePacket movedPacket = viewCache.PrepareGpuFrame(input);
        const std::vector<ve::VirtualShadowPageKey> firstKeys = ve::BuildVirtualShadowPageKeysForBounds(movedPacket.clipmaps, firstBounds);
        const std::vector<ve::VirtualShadowPageKey> movedKeys = ve::BuildVirtualShadowPageKeysForBounds(movedPacket.clipmaps, movedBounds);

        bool passed = true;
        passed &= Expect(firstPacket.enabled && firstPacket.gpuDriven && firstPacket.resetGpuCache,
                         "The first GPU frame should initialize an enabled cache and request its pending resource reset");
        passed &= Expect(movedPacket.enabled && movedPacket.gpuDriven && !movedPacket.resetGpuCache,
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
            passed &= Expect(movedConstants.entries[keyIndex].key0 == movedPacket.invalidatedPageKeys[keyIndex].key0 &&
                                 movedConstants.entries[keyIndex].key1 == movedPacket.invalidatedPageKeys[keyIndex].key1,
                             "GPU constants should preserve each invalidated absolute page key");
        }

        ve::VirtualShadowFramePacket fullContentPacket = movedPacket;
        fullContentPacket.invalidateAllGpuPages = true;
        const ve::VirtualShadowGpuConstants fullContentConstants = ve::BuildVirtualShadowGpuConstants(fullContentPacket);
        passed &= Expect(fullContentConstants.invalidationCount == ve::InvalidVirtualShadowGpuInvalidationCount,
                         "GPU constants should encode full resident-content invalidation with the sentinel count");

        input.frameIndex = 3;
        input.cameraCutRevision = 1;
        const ve::VirtualShadowFramePacket cameraCutPacket = viewCache.PrepareGpuFrame(input);
        passed &= Expect(!cameraCutPacket.resetGpuCache && cameraCutPacket.invalidatedPageKeys.empty(),
                         "A camera cut with stable casters should preserve compatible mappings and cached page content");

        input.frameIndex = 4;
        input.virtualShadowCacheRevision = 1;
        const ve::VirtualShadowFramePacket sceneChangePacket = viewCache.PrepareGpuFrame(input);
        passed &= Expect(sceneChangePacket.resetGpuCache, "A scene-content revision change should reset GPU page mappings");
        (void)viewCache.ConsumeGpuCacheReset();

        input.frameIndex = 5;
        input.light.direction = ve::Vector3::UnitX();
        const ve::VirtualShadowFramePacket directionPacket = viewCache.PrepareGpuFrame(input);
        passed &= Expect(directionPacket.resetGpuCache, "Changing the directional-light basis should reset GPU page mappings");
        return passed;
    }

    bool TestGpuViewCacheDormantPageInvalidation()
    {
        ve::VirtualShadowViewCache viewCache(520);
        const ve::Aabb firstBounds = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::Aabb movedBounds = ve::Aabb::FromCenterExtents(ve::Vector3(8.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f));
        const ve::VirtualShadowSceneItem firstItem = {91, 1, firstBounds, true, true, true, nullptr};

        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.cameraLocalToWorld = ve::Matrix44::Identity();
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f, 1};
        input.items = std::span<const ve::VirtualShadowSceneItem>(&firstItem, 1);
        const ve::VirtualShadowFramePacket firstPacket = viewCache.PrepareGpuFrame(input);
        const std::vector<ve::VirtualShadowPageKey> originalKeys = ve::BuildVirtualShadowPageKeysForBounds(firstPacket.clipmaps, firstBounds);
        (void)viewCache.ConsumeGpuCacheReset();

        const ve::VirtualShadowSceneItem movedItem = {91, 2, movedBounds, true, true, true, nullptr};
        input.frameIndex = 2;
        input.cameraLocalToWorld = ve::Matrix44::Translation(ve::Vector3(10000.0f, 10000.0f, 10000.0f));
        input.items = std::span<const ve::VirtualShadowSceneItem>(&movedItem, 1);
        const ve::VirtualShadowFramePacket movedWhileDormant = viewCache.PrepareGpuFrame(input);

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
        const ve::VirtualShadowSceneItem firstItem = {103, 1, firstBounds, true, true, true, nullptr};
        const ve::VirtualShadowSceneItem movedItem = {103, 2, movedBounds, true, true, true, nullptr};

        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.cameraLocalToWorld = ve::Matrix44::Identity();
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f, 1};
        input.items = std::span<const ve::VirtualShadowSceneItem>(&firstItem, 1);

        ve::VirtualShadowViewCache disabledCache(520);
        (void)disabledCache.PrepareGpuFrame(input);
        (void)disabledCache.ConsumeGpuCacheReset();
        input.frameIndex = 2;
        input.light.enabled = false;
        input.light.shadowDistance = 400.0f;
        const ve::VirtualShadowFramePacket disabledPacket = disabledCache.PrepareGpuFrame(input);
        input.frameIndex = 3;
        input.light.enabled = true;
        const ve::VirtualShadowFramePacket reenabledPacket = disabledCache.PrepareGpuFrame(input);

        ve::VirtualShadowViewCache invalidProjectionCache(520);
        input.frameIndex = 1;
        input.light.shadowDistance = 200.0f;
        input.items = std::span<const ve::VirtualShadowSceneItem>(&firstItem, 1);
        (void)invalidProjectionCache.PrepareGpuFrame(input);
        (void)invalidProjectionCache.ConsumeGpuCacheReset();
        input.frameIndex = 2;
        input.viewProjection = ve::Matrix44::Zero();
        input.items = std::span<const ve::VirtualShadowSceneItem>(&movedItem, 1);
        const ve::VirtualShadowFramePacket invalidProjectionPacket = invalidProjectionCache.PrepareGpuFrame(input);
        input.frameIndex = 3;
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        const ve::VirtualShadowFramePacket recoveredPacket = invalidProjectionCache.PrepareGpuFrame(input);

        bool passed = true;
        passed &= Expect(!disabledPacket.enabled && reenabledPacket.resetGpuCache,
                         "A shadow-distance change while disabled should reset mappings on the next enabled GPU frame");
        passed &= Expect(!invalidProjectionPacket.enabled && !recoveredPacket.invalidatedPageKeys.empty(),
                         "An invalid projection frame should not consume caster changes before a GPU clear pass can receive them");
        return passed;
    }

    bool TestGpuViewCacheDefersSceneRevisionReset()
    {
        ve::VirtualShadowViewCache viewCache(520);
        const ve::VirtualShadowSceneItem item = {
            131,
            1,
            ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3(0.25f, 0.25f, 0.25f)),
            true,
            true,
            true,
            nullptr,
        };

        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.virtualShadowCacheRevision = 0;
        input.cameraLocalToWorld = ve::Matrix44::Identity();
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f, 1};
        input.items = std::span<const ve::VirtualShadowSceneItem>(&item, 1);
        (void)viewCache.PrepareGpuFrame(input);
        (void)viewCache.ConsumeGpuCacheReset();

        input.frameIndex = 2;
        input.virtualShadowCacheRevision = 1;
        input.viewProjection = ve::Matrix44::Zero();
        const ve::VirtualShadowFramePacket invalidPacket = viewCache.PrepareGpuFrame(input);

        input.frameIndex = 3;
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        const ve::VirtualShadowFramePacket recoveredPacket = viewCache.PrepareGpuFrame(input);

        bool passed = true;
        passed &= Expect(!invalidPacket.enabled, "An invalid projection should not submit a GPU VSM frame");
        passed &= Expect(recoveredPacket.resetGpuCache,
                         "A scene revision observed during an invalid frame should reset the next valid GPU VSM frame");
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
        return passed;
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

    bool TestRenderViewStateVirtualShadowCacheRevision()
    {
        ve::RenderViewState viewState(ve::RenderViewStateDesc{"RevisionTest", 520});
        const std::shared_ptr<ve::RTRenderViewState> rtViewState = viewState.GetRTRenderViewState();

        bool passed = true;
        passed &= Expect(rtViewState->GetVirtualShadowCacheRevision() == 0,
                         "A new render view should start at virtual-shadow cache revision zero");
        passed &= Expect(rtViewState->GetCameraCutRevision() == 0, "A new render view should start at camera-cut revision zero");

        viewState.RequestVirtualShadowCacheReset();
        passed &= Expect(rtViewState->GetVirtualShadowCacheRevision() == 1,
                         "A virtual-shadow cache reset request should increment the render-thread revision");
        passed &= Expect(rtViewState->GetCameraCutRevision() == 0, "A virtual-shadow cache reset request should not imply a camera cut");
        return passed;
    }
} // namespace

int main()
{
    if (TestPageKeysAndResidentTable() && TestResidentTableBoundsProbes() && TestPhysicalPageCacheLifecycle() && TestPageCachePressurePriorityAndIsolation() &&
        TestClipmapQuantization() && TestReceiverRequests() && TestCasterInvalidationHistory() && TestLargePageCacheIsolation() &&
        TestCpuViewCachePacketAndOverlap() && TestGpuViewCacheLocalInvalidation() && TestGpuViewCacheDefersUnsubmittedState() &&
        TestGpuViewCacheDormantPageInvalidation() && TestGpuViewCacheDefersSceneRevisionReset() && TestVirtualShadowNormalizedPageGutter() &&
        TestWorldDepthBiasConversion() && TestRenderViewStateVirtualShadowCacheRevision())
    {
        std::cout << "VEngineVirtualShadowTests passed" << '\n';
        return 0;
    }

    return 1;
}
