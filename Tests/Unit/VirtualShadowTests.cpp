#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"

#include <iostream>
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
            const ve::VirtualShadowPageKey key = ve::VirtualShadowPageKey::Create(static_cast<ve::Int32>(index), static_cast<ve::Int32>(index / 64), index % 4, 0);
            passed &= Expect(loadedTable.Insert(key, index), "A half-full resident table should accept deterministic entries");
        }
        for (ve::UInt32 index = 0; index < 1024; ++index)
        {
            const ve::VirtualShadowPageKey key = ve::VirtualShadowPageKey::Create(static_cast<ve::Int32>(index), static_cast<ve::Int32>(index / 64), index % 4, 0);
            passed &= Expect(loadedTable.Find(key).value_or(ve::InvalidVirtualShadowPhysicalPage) == index, "A half-full resident table should resolve every entry");
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
        passed &= Expect(secondOrigin.x == 131 && secondOrigin.y == 1, "Physical slots should reserve gutters on both sides");

        return passed;
    }
} // namespace

int main()
{
    if (TestPageKeysAndResidentTable() && TestResidentTableBoundsProbes() && TestPhysicalPageCacheLifecycle() && TestPageCachePressurePriorityAndIsolation())
    {
        std::cout << "VEngineVirtualShadowTests passed" << '\n';
        return 0;
    }

    return 1;
}
