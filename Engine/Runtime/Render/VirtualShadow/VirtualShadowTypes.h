#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector4.h"

#include <cmath>
#include <limits>

namespace ve
{
    constexpr UInt32 InvalidVirtualShadowPhysicalPage = std::numeric_limits<UInt32>::max();
    constexpr UInt32 VirtualShadowClipmapLevelCount = 4;
    constexpr UInt32 VirtualShadowVirtualResolution = 16384;
    constexpr UInt32 VirtualShadowPageSize = 128;
    constexpr UInt32 VirtualShadowPhysicalPageSize = 128;
    constexpr UInt32 VirtualShadowPagesPerAxis = VirtualShadowVirtualResolution / VirtualShadowPageSize;
    constexpr UInt32 VirtualShadowPageGutter = 1;
    constexpr UInt32 VirtualShadowPhysicalPageContentSize = VirtualShadowPhysicalPageSize - (2u * VirtualShadowPageGutter);
    constexpr UInt32 VirtualShadowPageTableCapacity = 2048;
    constexpr UInt32 VirtualShadowPageTableMaxProbes = 16;
    constexpr Int32 VirtualShadowMinimumDepthEpoch = -(1 << 23);
    constexpr Int32 VirtualShadowMaximumDepthEpoch = (1 << 23) - 1;

    [[nodiscard]] inline bool TryQuantizeVirtualShadowCoordinate(Float32 coordinate, Float32 step, Int32& result) noexcept
    {
        if (!std::isfinite(coordinate) || !std::isfinite(step) || step <= 0.0f)
        {
            return false;
        }

        const double quantized = std::floor(static_cast<double>(coordinate) / static_cast<double>(step));
        if (!std::isfinite(quantized) || quantized < static_cast<double>(std::numeric_limits<Int32>::min()) ||
            quantized > static_cast<double>(std::numeric_limits<Int32>::max()))
        {
            return false;
        }

        result = static_cast<Int32>(quantized);
        return true;
    }

    [[nodiscard]] inline bool TryBuildVirtualShadowWorkingRegion(Int32 originPage, Int32& minimumPage, Int32& maximumPage) noexcept
    {
        const Int64 minimum = static_cast<Int64>(originPage) - static_cast<Int64>(VirtualShadowPagesPerAxis / 2u);
        const Int64 maximum = minimum + static_cast<Int64>(VirtualShadowPagesPerAxis) - 1;
        if (minimum < std::numeric_limits<Int16>::min() || maximum > std::numeric_limits<Int16>::max())
        {
            return false;
        }

        minimumPage = static_cast<Int32>(minimum);
        maximumPage = static_cast<Int32>(maximum);
        return true;
    }

    [[nodiscard]] inline bool TryQuantizeVirtualShadowPageRange(
        Float32 minimumCoordinate, Float32 maximumCoordinate, Float32 pageWorldSize, Int32& minimumPage, Int32& maximumPage) noexcept
    {
        return minimumCoordinate <= maximumCoordinate && TryQuantizeVirtualShadowCoordinate(minimumCoordinate, pageWorldSize, minimumPage) &&
               TryQuantizeVirtualShadowCoordinate(maximumCoordinate, pageWorldSize, maximumPage);
    }

    [[nodiscard]] inline bool IsVirtualShadowDepthEpochRepresentable(Int32 depthEpoch) noexcept
    {
        return depthEpoch >= VirtualShadowMinimumDepthEpoch && depthEpoch <= VirtualShadowMaximumDepthEpoch;
    }

    struct VirtualShadowPageKey
    {
        UInt32 key0 = 0xFFFFFFFFu;
        UInt32 key1 = 0xFFFFFFFFu;

        [[nodiscard]] static VirtualShadowPageKey Create(Int32 pageX, Int32 pageY, UInt32 clipmapLevel, Int32 depthEpoch) noexcept
        {
            constexpr Int32 MinimumCoordinate = std::numeric_limits<Int16>::min();
            constexpr Int32 MaximumCoordinate = std::numeric_limits<Int16>::max();
            if (pageX < MinimumCoordinate || pageX > MaximumCoordinate || pageY < MinimumCoordinate || pageY > MaximumCoordinate ||
                clipmapLevel >= VirtualShadowClipmapLevelCount || !IsVirtualShadowDepthEpochRepresentable(depthEpoch))
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

    constexpr UInt32 VirtualShadowGpuPageEntryValid = 1u << 0u;

    struct alignas(16) VirtualShadowGpuClipmap
    {
        Vector4 lightOriginAndPageWorldSize = Vector4::Zero();
        Vector4 radiusAndDepthRange = Vector4::Zero();
        Int32 originPageX = 0;
        Int32 originPageY = 0;
        Int32 depthEpoch = 0;
        Int32 padding = 0;
    };

    struct alignas(16) VirtualShadowGpuConstants
    {
        Vector4 lightRight = Vector4::Zero();
        Vector4 lightUp = Vector4::Zero();
        Vector4 lightDirection = Vector4::Zero();
        Vector4 atlasAndBias = Vector4::Zero();
        VirtualShadowGpuClipmap clipmaps[VirtualShadowClipmapLevelCount] = {};
        UInt32 enabled = 0;
        UInt32 atlasExtent = 0;
        UInt32 physicalPageSize = VirtualShadowPhysicalPageSize;
        UInt32 clipmapLevelCount = VirtualShadowClipmapLevelCount;
        VirtualShadowGpuPageEntry entries[VirtualShadowPageTableCapacity] = {};
    };

    static_assert(sizeof(VirtualShadowGpuClipmap) == 48);
    static_assert(sizeof(VirtualShadowGpuConstants) <= 65536);
} // namespace ve
