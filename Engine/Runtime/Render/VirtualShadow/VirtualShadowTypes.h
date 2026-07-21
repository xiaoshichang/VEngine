#pragma once

#include "Engine/Runtime/Core/Types.h"

#include <limits>

namespace ve
{
    constexpr UInt32 InvalidVirtualShadowPhysicalPage = std::numeric_limits<UInt32>::max();
    constexpr UInt32 VirtualShadowClipmapLevelCount = 4;
    constexpr UInt32 VirtualShadowVirtualResolution = 16384;
    constexpr UInt32 VirtualShadowPageSize = 128;
    constexpr UInt32 VirtualShadowPagesPerAxis = VirtualShadowVirtualResolution / VirtualShadowPageSize;
    constexpr UInt32 VirtualShadowPageGutter = 1;
    constexpr UInt32 VirtualShadowPageTableCapacity = 2048;
    constexpr UInt32 VirtualShadowPageTableMaxProbes = 16;

    struct VirtualShadowPageKey
    {
        UInt32 key0 = 0xFFFFFFFFu;
        UInt32 key1 = 0xFFFFFFFFu;

        [[nodiscard]] static VirtualShadowPageKey Create(Int32 pageX, Int32 pageY, UInt32 clipmapLevel, Int32 depthEpoch) noexcept
        {
            constexpr Int32 MinimumCoordinate = std::numeric_limits<Int16>::min();
            constexpr Int32 MaximumCoordinate = std::numeric_limits<Int16>::max();
            constexpr Int32 MinimumDepthEpoch = -(1 << 23);
            constexpr Int32 MaximumDepthEpoch = (1 << 23) - 1;

            if (pageX < MinimumCoordinate || pageX > MaximumCoordinate || pageY < MinimumCoordinate || pageY > MaximumCoordinate ||
                clipmapLevel >= VirtualShadowClipmapLevelCount || depthEpoch < MinimumDepthEpoch || depthEpoch > MaximumDepthEpoch)
            {
                return {};
            }

            VirtualShadowPageKey result;
            result.key0 = static_cast<UInt32>(static_cast<UInt16>(pageX)) | (static_cast<UInt32>(static_cast<UInt16>(pageY)) << 16u);
            result.key1 = clipmapLevel | ((static_cast<UInt32>(depthEpoch) & 0x00FFFFFFu) << 8u);
            return result;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return key0 != 0xFFFFFFFFu || key1 != 0xFFFFFFFFu;
        }

        [[nodiscard]] Int32 GetPageX() const noexcept
        {
            return static_cast<Int16>(key0 & 0xFFFFu);
        }

        [[nodiscard]] Int32 GetPageY() const noexcept
        {
            return static_cast<Int16>(key0 >> 16u);
        }

        [[nodiscard]] UInt32 GetClipmapLevel() const noexcept
        {
            return key1 & 0xFFu;
        }

        [[nodiscard]] Int32 GetDepthEpoch() const noexcept
        {
            UInt32 packedEpoch = key1 >> 8u;
            if ((packedEpoch & 0x00800000u) != 0u)
            {
                packedEpoch |= 0xFF000000u;
            }
            return static_cast<Int32>(packedEpoch);
        }

        [[nodiscard]] bool operator==(const VirtualShadowPageKey&) const noexcept = default;
    };

    struct VirtualShadowPageKeyHash
    {
        [[nodiscard]] SizeT operator()(const VirtualShadowPageKey& key) const noexcept;
    };

    [[nodiscard]] inline UInt32 HashVirtualShadowPageKey(const VirtualShadowPageKey& key) noexcept
    {
        UInt32 hash = (key.key0 * 0x9E3779B1u) ^ (key.key1 * 0x85EBCA77u);
        hash ^= hash >> 16u;
        return hash;
    }

    inline SizeT VirtualShadowPageKeyHash::operator()(const VirtualShadowPageKey& key) const noexcept
    {
        return HashVirtualShadowPageKey(key);
    }

    struct VirtualShadowPageRequest
    {
        VirtualShadowPageKey key;
        UInt32 priority = 0;
    };

    struct alignas(16) VirtualShadowGpuPageEntry
    {
        UInt32 key0 = 0xFFFFFFFFu;
        UInt32 key1 = 0xFFFFFFFFu;
        UInt32 physicalPageIndex = InvalidVirtualShadowPhysicalPage;
        UInt32 flags = 0;
    };

    static_assert(sizeof(VirtualShadowGpuPageEntry) == 16);
} // namespace ve
