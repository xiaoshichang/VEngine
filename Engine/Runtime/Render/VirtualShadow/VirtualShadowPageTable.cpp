#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h"

#include <algorithm>

namespace ve
{
    void VirtualShadowPageTable::Clear() noexcept
    {
        std::ranges::fill(entries_, VirtualShadowGpuPageEntry{});
        size_ = 0;
    }

    bool VirtualShadowPageTable::Insert(VirtualShadowPageKey key, UInt32 physicalPageIndex) noexcept
    {
        if (!key.IsValid() || physicalPageIndex == InvalidVirtualShadowPhysicalPage)
        {
            return false;
        }

        const UInt32 firstIndex = HashVirtualShadowPageKey(key) & (VirtualShadowPageTableCapacity - 1u);
        for (UInt32 probe = 0; probe < VirtualShadowPageTableMaxProbes; ++probe)
        {
            VirtualShadowGpuPageEntry& entry = entries_[(firstIndex + probe) & (VirtualShadowPageTableCapacity - 1u)];
            if (entry.physicalPageIndex == InvalidVirtualShadowPhysicalPage)
            {
                entry = VirtualShadowGpuPageEntry{key.key0, key.key1, physicalPageIndex, VirtualShadowGpuPageEntryValid};
                ++size_;
                return true;
            }

            if (entry.key0 == key.key0 && entry.key1 == key.key1)
            {
                entry.physicalPageIndex = physicalPageIndex;
                return true;
            }
        }

        return false;
    }

    std::optional<UInt32> VirtualShadowPageTable::Find(VirtualShadowPageKey key) const noexcept
    {
        if (!key.IsValid())
        {
            return std::nullopt;
        }

        const UInt32 firstIndex = HashVirtualShadowPageKey(key) & (VirtualShadowPageTableCapacity - 1u);
        for (UInt32 probe = 0; probe < VirtualShadowPageTableMaxProbes; ++probe)
        {
            const VirtualShadowGpuPageEntry& entry = entries_[(firstIndex + probe) & (VirtualShadowPageTableCapacity - 1u)];
            if (entry.physicalPageIndex == InvalidVirtualShadowPhysicalPage)
            {
                return std::nullopt;
            }

            if (entry.key0 == key.key0 && entry.key1 == key.key1)
            {
                return entry.physicalPageIndex;
            }
        }

        return std::nullopt;
    }

    UInt32 VirtualShadowPageTable::GetSize() const noexcept
    {
        return size_;
    }

    std::span<const VirtualShadowGpuPageEntry> VirtualShadowPageTable::GetGpuEntries() const noexcept
    {
        return entries_;
    }
} // namespace ve
