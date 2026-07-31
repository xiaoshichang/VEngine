#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector4.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace ve
{
    /// Finest active directional-light clipmap level. Its world-space radius is 2^level, so level 7 covers a radius of 128 world units.
    constexpr UInt32 VirtualShadowClipmapFirstLevel = 7;

    /// Coarsest active directional-light clipmap level, inclusive. Increasing it extends shadow coverage but adds logical-page storage.
    constexpr UInt32 VirtualShadowClipmapLastLevel = 10;

    /// Fixed clipmap-array capacity shared by the CPU constant-buffer layout and all VSM shaders; it is not the active level count.
    constexpr UInt32 VirtualShadowMaximumClipmapLevelCount = 24;
    static_assert(VirtualShadowClipmapFirstLevel <= VirtualShadowClipmapLastLevel);

    /// Number of active clipmap levels in the inclusive [FirstLevel, LastLevel] range.
    constexpr UInt32 VirtualShadowClipmapLevelCount = VirtualShadowClipmapLastLevel - VirtualShadowClipmapFirstLevel + 1u;
    static_assert(VirtualShadowClipmapLevelCount <= VirtualShadowMaximumClipmapLevelCount);

    /// Virtual resolution, in texels per axis, of each clipmap level.
    constexpr UInt32 VirtualShadowVirtualResolution = 16384;

    /// Logical page width and height, in virtual-shadow texels.
    constexpr UInt32 VirtualShadowPageSize = 128;

    /// Physical atlas slot width and height, in texels. It equals the logical page size because VSM currently has no gutter.
    /// Atlas value zero represents empty space; valid depths are stored as positive reversed-float bit patterns so unsigned atomic maximum preserves the
    /// nearest caster.
    constexpr UInt32 VirtualShadowPhysicalPageSize = 128;

    /// Number of logical pages per axis in one clipmap level.
    constexpr UInt32 VirtualShadowPagesPerAxis = VirtualShadowVirtualResolution / VirtualShadowPageSize;

    /// Total number of logical page-table entries reserved for one view across all active clipmap levels.
    constexpr UInt32 VirtualShadowLogicalPageCount = VirtualShadowClipmapLevelCount * VirtualShadowPagesPerAxis * VirtualShadowPagesPerAxis;

    /// Upper bound on physical pages in the render-system-wide shared atlas and metadata pool.
    constexpr UInt32 VirtualShadowMaxPhysicalPageCount = 2048;

    /// Maximum number of individual page invalidations uploaded to the GPU for one view update.
    constexpr UInt32 VirtualShadowMaxInvalidationPageCount = 2048;

    /// Sentinel invalidation count meaning "invalidate every cached page belonging to this view".
    constexpr UInt32 InvalidVirtualShadowGpuInvalidationCount = std::numeric_limits<UInt32>::max();

    /// Reserved view identifier used when no valid render view is assigned.
    constexpr UInt32 InvalidVirtualShadowViewID = 0;

    /// Largest representable view identifier; a page key reserves the upper 24 bits of key1 for the view ID.
    constexpr UInt32 VirtualShadowMaximumViewID = 0x00FFFFFFu;

    /// CPU-side recording evidence for one view's indexed shadow-caster draws.
    struct VirtualShadowViewDrawDiagnostics
    {
        /// View whose shadow-caster draws were recorded.
        UInt32 viewID = InvalidVirtualShadowViewID;

        /// Number of indexed draw calls recorded for that view in the VSM page-render pass.
        UInt32 indexedDrawCount = 0;
    };

    /// Quantizes a world-space coordinate to its containing grid cell using floor(coordinate / step).
    /// @param coordinate Coordinate to quantize, in the same unit as step.
    /// @param step Positive grid-cell size.
    /// @param result Receives the signed grid coordinate when the input and result are representable.
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

    /// Builds the inclusive absolute-page range covered by one clipmap axis around an origin page.
    /// @param originPage Absolute page containing the snapped clipmap origin.
    /// @param minimumPage Receives the first covered absolute page.
    /// @param maximumPage Receives the last covered absolute page.
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

    /// Quantizes an inclusive world-space interval to an inclusive absolute-page interval.
    /// @param minimumCoordinate Lower interval endpoint in world units.
    /// @param maximumCoordinate Upper interval endpoint in world units.
    /// @param pageWorldSize Width of one logical page in world units.
    /// @param minimumPage Receives the absolute page containing minimumCoordinate.
    /// @param maximumPage Receives the absolute page containing maximumCoordinate.
    [[nodiscard]] inline bool TryQuantizeVirtualShadowPageRange(
        Float32 minimumCoordinate, Float32 maximumCoordinate, Float32 pageWorldSize, Int32& minimumPage, Int32& maximumPage) noexcept
    {
        return minimumCoordinate <= maximumCoordinate && TryQuantizeVirtualShadowCoordinate(minimumCoordinate, pageWorldSize, minimumPage) &&
               TryQuantizeVirtualShadowCoordinate(maximumCoordinate, pageWorldSize, maximumPage);
    }

    /// Converts a non-negative light-space depth bias from world units to normalized shadow depth.
    /// @param worldDepthBias Bias in world units along the light depth axis.
    /// @param depthRange Full clipmap depth range in world units.
    [[nodiscard]] inline Float32 ConvertVirtualShadowWorldDepthBiasToNormalized(Float32 worldDepthBias, Float32 depthRange) noexcept
    {
        if (!std::isfinite(worldDepthBias) || !std::isfinite(depthRange) || worldDepthBias < 0.0f || depthRange <= 0.0f)
        {
            return 0.0f;
        }

        return worldDepthBias / depthRange;
    }

    /// Returns the number of complete physical-page slots that fit in a square atlas.
    /// @param atlasExtent Atlas width and height in texels.
    [[nodiscard]] inline UInt32 GetVirtualShadowPhysicalPageCapacity(UInt32 atlasExtent) noexcept
    {
        const UInt32 pagesPerAxis = atlasExtent / VirtualShadowPhysicalPageSize;
        return pagesPerAxis * pagesPerAxis;
    }

    struct VirtualShadowPageKey
    {
        /// Packed signed absolute page coordinates: pageX in bits [0, 15], pageY in bits [16, 31].
        UInt32 key0 = 0xFFFFFFFFu;

        /// Packed active clipmap-array index in bits [0, 7] and view ID in bits [8, 31].
        UInt32 key1 = 0xFFFFFFFFu;

        /// Creates a cache key for one absolute virtual page; returns the invalid all-ones key if any component is out of range.
        [[nodiscard]] static VirtualShadowPageKey Create(Int32 pageX, Int32 pageY, UInt32 clipmapLevel, UInt32 viewID) noexcept
        {
            constexpr Int32 MinimumCoordinate = std::numeric_limits<Int16>::min();
            constexpr Int32 MaximumCoordinate = std::numeric_limits<Int16>::max();
            if (pageX < MinimumCoordinate || pageX > MaximumCoordinate || pageY < MinimumCoordinate || pageY > MaximumCoordinate ||
                clipmapLevel >= VirtualShadowClipmapLevelCount || viewID == InvalidVirtualShadowViewID || viewID > VirtualShadowMaximumViewID)
            {
                return {};
            }

            VirtualShadowPageKey result;
            result.key0 = static_cast<UInt32>(static_cast<UInt16>(pageX)) | (static_cast<UInt32>(static_cast<UInt16>(pageY)) << 16u);
            result.key1 = clipmapLevel | (viewID << 8u);
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

        [[nodiscard]] UInt32 GetViewID() const noexcept
        {
            return key1 >> 8u;
        }

        [[nodiscard]] bool operator==(const VirtualShadowPageKey&) const noexcept = default;
    };

    struct VirtualShadowPageKeyHash
    {
        /// Produces a hash suitable for CPU-side unordered containers keyed by VirtualShadowPageKey.
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

    struct alignas(16) VirtualShadowGpuInvalidationEntry
    {
        /// Packed absolute page X/Y coordinates; layout matches VirtualShadowPageKey::key0.
        UInt32 key0 = 0xFFFFFFFFu;

        /// Packed clipmap-array index and view ID; layout matches VirtualShadowPageKey::key1.
        UInt32 key1 = 0xFFFFFFFFu;

        /// Constant-buffer padding; unused by VSM logic.
        UInt32 padding0 = 0;

        /// Constant-buffer padding; unused by VSM logic.
        UInt32 padding1 = 0;
    };

    static_assert(sizeof(VirtualShadowGpuInvalidationEntry) == 16);

    struct alignas(16) VirtualShadowGpuPhysicalPage
    {
        /// Virtual page currently stored in this physical slot: packed signed absolute X/Y coordinates.
        UInt32 key0 = 0xFFFFFFFFu;

        /// Virtual page currently stored in this physical slot: packed clipmap-array index and view ID.
        UInt32 key1 = 0xFFFFFFFFu;

        /// Lower 32 bits of the most recent frame index that requested this page; used for LRU eviction.
        UInt32 lastUsedFrame = 0;

        /// Bit mask composed from the VirtualShadowGpuPhysicalPage* flags below.
        UInt32 flags = 0;
    };

    /// The physical slot contains a valid virtual-page mapping.
    constexpr UInt32 VirtualShadowGpuPhysicalPageValid = 1u << 0u;

    /// The physical slot's depth content must be cleared and redrawn before sampling.
    constexpr UInt32 VirtualShadowGpuPhysicalPageDirty = 1u << 1u;

    /// The page was requested by at least one active view during the current render update.
    constexpr UInt32 VirtualShadowGpuPhysicalPageRequested = 1u << 2u;

    /// The page-render pass wrote this physical slot during the current render update.
    constexpr UInt32 VirtualShadowGpuPhysicalPageRenderedThisFrame = 1u << 3u;

    /// UInt32 indices used by shaders to atomically update the packed VSM statistics buffer.
    enum class VirtualShadowGpuStatisticIndex : UInt32
    {
        /// Number of valid physical slots after finalizing the current render update.
        AllocatedPhysicalPages,

        /// Number of unique logical pages requested by receiver pixels across the processed views.
        RequestedPages,

        /// Number of requested pages that found an existing physical mapping; dirty hits can also be counted as redrawn.
        CachedPages,

        /// Number of requested pages assigned to a new or repurposed physical slot.
        NewlyAllocatedPages,

        /// Number of requested physical pages whose depth content was rendered during this update.
        RedrawnPages,

        /// Number of requested logical pages that could not obtain a physical slot.
        UnmappedPages,

        /// Number of UInt32 counters in the statistics buffer; not itself a statistic.
        Count,
    };

    /// CPU mirror of the packed GPU statistics buffer. Field order must match VirtualShadowGpuStatisticIndex.
    struct alignas(16) VirtualShadowGpuStatistics
    {
        /// Number of valid physical slots after finalizing the current render update.
        UInt32 allocatedPhysicalPages = 0;

        /// Number of unique logical pages requested by receiver pixels across the processed views.
        UInt32 requestedPages = 0;

        /// Number of requested pages that found an existing physical mapping; it is not mutually exclusive with redrawnPages.
        UInt32 cachedPages = 0;

        /// Number of requested pages assigned to a new or repurposed physical slot.
        UInt32 newlyAllocatedPages = 0;

        /// Number of physical pages whose shadow depth was rendered during this update.
        UInt32 redrawnPages = 0;

        /// Number of requested logical pages that could not obtain a physical slot.
        UInt32 unmappedPages = 0;

        /// Padding that rounds the structure to two 16-byte constant-buffer registers.
        UInt32 padding[2] = {};
    };

    /// GPU description of one active directional-light clipmap level.
    struct alignas(16) VirtualShadowGpuClipmap
    {
        /// Light-space clipmap origin XYZ in world units and logical-page width in W.
        Vector4 lightOriginAndPageWorldSize = Vector4::Zero();

        /// World-space radius in X, minimum/maximum light-space depth in Y/Z, and depth-origin snapping step in W.
        Vector4 radiusAndDepthRange = Vector4::Zero();

        /// Absolute logical-page X coordinate of the snapped clipmap origin.
        Int32 originPageX = 0;

        /// Absolute logical-page Y coordinate of the snapped clipmap origin.
        Int32 originPageY = 0;

        /// Quantized light-space depth-origin index; depth center equals depthEpoch * radiusAndDepthRange.w.
        Int32 depthEpoch = 0;

        /// Constant-buffer padding; unused by VSM logic.
        Int32 padding = 0;
    };

    /// CPU mirror of the VSM shader constant buffer. Keep field order and array capacities synchronized with every embedded VSM HLSL declaration.
    struct alignas(16) VirtualShadowGpuConstants
    {
        /// World-to-light basis right axis in XYZ; W is unused.
        Vector4 lightRight = Vector4::Zero();

        /// World-to-light basis up axis in XYZ; W is unused.
        Vector4 lightUp = Vector4::Zero();

        /// World-to-light forward/depth axis in XYZ; W is unused.
        Vector4 lightDirection = Vector4::Zero();

        /// Atlas reciprocal extent in X, normalized depth bias in Y, world-space normal bias in Z; W is unused.
        Vector4 atlasAndBias = Vector4::Zero();

        /// Fixed-capacity GPU clipmap array; only the first clipmapLevelCount entries are active.
        VirtualShadowGpuClipmap clipmaps[VirtualShadowMaximumClipmapLevelCount] = {};

        /// Square physical-atlas width and height in texels.
        UInt32 atlasExtent = 0;

        /// Width and height of one physical atlas slot in texels.
        UInt32 physicalPageSize = VirtualShadowPhysicalPageSize;

        /// Number of active entries at the front of clipmaps.
        UInt32 clipmapLevelCount = VirtualShadowClipmapLevelCount;

        /// Constant-buffer padding; unused by VSM logic.
        UInt32 atlasPadding = 0;

        /// Current receiver view's inverse view-projection matrix, used to reconstruct world positions from depth.
        Matrix44 inverseViewProjection = Matrix44::Identity();

        /// Receiver depth texture width in pixels.
        UInt32 screenWidth = 0;

        /// Receiver depth texture height in pixels.
        UInt32 screenHeight = 0;

        /// Number of usable entries at the front of the shared physical-page metadata buffer.
        UInt32 physicalPageCapacity = 0;

        /// Lower 32 bits of the render-system frame index; used as request generation and LRU timestamp.
        UInt32 frameIndex = 0;

        /// Boolean flag: nonzero clears all shared physical-page mappings before processing requests.
        UInt32 resetCache = 0;

        /// Pass-specific scalar; ResolvePageHits currently uses nonzero to include persisted requests in requestedPages.
        UInt32 passLevel = 0;

        /// Number of valid invalidationEntries; InvalidVirtualShadowGpuInvalidationCount invalidates every page for viewID.
        UInt32 invalidationCount = 0;

        /// Boolean visualization mode consumed by the opaque scene shader; nonzero displays VSM page diagnostics.
        UInt32 debugMode = 0;

        /// Receiver camera world position in XYZ; W is not used by VSM request selection.
        Vector4 cameraWorldPosition = Vector4::Zero();

        /// Normalized receiver camera forward direction in XYZ; W is unused.
        Vector4 cameraWorldForward = Vector4(0.0f, 0.0f, 1.0f, 0.0f);

        /// Explicit virtual pages whose cached physical content must be marked dirty.
        VirtualShadowGpuInvalidationEntry invalidationEntries[VirtualShadowMaxInvalidationPageCount] = {};

        /// Stable nonzero view identifier packed into every virtual-page cache key.
        UInt32 viewID = InvalidVirtualShadowViewID;

        /// Constant-buffer padding that keeps the trailing view data 16-byte aligned.
        UInt32 viewIDPadding[3] = {};
    };

    static_assert(sizeof(VirtualShadowGpuClipmap) == 48);
    static_assert(sizeof(VirtualShadowGpuPhysicalPage) == 16);
    static_assert(sizeof(VirtualShadowGpuStatistics) == 32);
    static_assert(alignof(VirtualShadowGpuStatistics) == 16);
    static_assert(offsetof(VirtualShadowGpuStatistics, allocatedPhysicalPages) == 0);
    static_assert(offsetof(VirtualShadowGpuStatistics, requestedPages) == sizeof(UInt32));
    static_assert(offsetof(VirtualShadowGpuStatistics, cachedPages) == 2 * sizeof(UInt32));
    static_assert(offsetof(VirtualShadowGpuStatistics, newlyAllocatedPages) == 3 * sizeof(UInt32));
    static_assert(offsetof(VirtualShadowGpuStatistics, redrawnPages) == 4 * sizeof(UInt32));
    static_assert(offsetof(VirtualShadowGpuStatistics, unmappedPages) == 5 * sizeof(UInt32));
    static_assert(alignof(VirtualShadowGpuConstants) == 16);
    static_assert(sizeof(VirtualShadowGpuConstants) % alignof(VirtualShadowGpuConstants) == 0);
    static_assert(offsetof(VirtualShadowGpuConstants, viewID) ==
                  offsetof(VirtualShadowGpuConstants, invalidationEntries) + sizeof(VirtualShadowGpuConstants::invalidationEntries));
    static_assert(sizeof(VirtualShadowGpuConstants) - offsetof(VirtualShadowGpuConstants, viewID) == 16);
    static_assert(sizeof(VirtualShadowGpuConstants) <= 65536);
} // namespace ve
