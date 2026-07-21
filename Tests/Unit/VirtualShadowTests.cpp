#include "Engine/Runtime/Render/RenderCameraMath.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"

#include <algorithm>
#include <iostream>
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
        return passed;
    }

    bool ContainsKey(std::span<const ve::VirtualShadowPageKey> keys, ve::VirtualShadowPageKey expected)
    {
        return std::ranges::find(keys, expected) != keys.end();
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
        const ve::VirtualShadowSceneItem item = {42, 1, bounds, true, true, nullptr};
        ve::VirtualShadowPrepareInput input;
        input.frameIndex = 1;
        input.cameraCutRevision = 0;
        input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 1.0f, 0.1f, 100.0f);
        input.items = std::span<const ve::VirtualShadowSceneItem>(&item, 1);
        input.light = {true, ve::Vector3(0.0f, -1.0f, 0.0f), 200.0f, 0.001f, 0.05f, 1};

        const ve::VirtualShadowFramePacket packet = viewCache.PrepareFrame(input);
        bool foundCaster = false;
        for (const ve::VirtualShadowDirtyPageDraw& draw : packet.dirtyPages)
        {
            foundCaster |= std::ranges::find(draw.casterRenderItemIDs, 42u) != draw.casterRenderItemIDs.end();
        }

        bool passed = true;
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
        return passed;
    }
} // namespace

int main()
{
    if (TestPageKeysAndResidentTable() && TestResidentTableBoundsProbes() && TestPhysicalPageCacheLifecycle() && TestPageCachePressurePriorityAndIsolation() &&
        TestClipmapQuantization() && TestReceiverRequests() && TestCasterInvalidationHistory() && TestLargePageCacheIsolation() &&
        TestCpuViewCachePacketAndOverlap())
    {
        std::cout << "VEngineVirtualShadowTests passed" << '\n';
        return 0;
    }

    return 1;
}
