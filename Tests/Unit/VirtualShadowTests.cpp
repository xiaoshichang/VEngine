#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h"
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
} // namespace

int main()
{
    if (TestPageKeysAndResidentTable() && TestResidentTableBoundsProbes())
    {
        std::cout << "VEngineVirtualShadowTests passed" << '\n';
        return 0;
    }

    return 1;
}
