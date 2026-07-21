#pragma once

#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h"

#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace ve
{
    enum class VirtualShadowPhysicalPageState
    {
        Free,
        ResidentClean,
        ResidentDirty,
    };

    struct VirtualShadowPhysicalPage
    {
        VirtualShadowPhysicalPageState state = VirtualShadowPhysicalPageState::Free;
        VirtualShadowPageKey key;
        UInt64 lastUsedFrame = 0;
        UInt32 generation = 0;
        bool pinnedThisFrame = false;
    };

    struct VirtualShadowRequestResolution
    {
        UInt32 requested = 0;
        UInt32 hits = 0;
        UInt32 allocated = 0;
        UInt32 evicted = 0;
        UInt32 missing = 0;
    };

    struct VirtualShadowPhysicalPageOrigin
    {
        UInt32 x = 0;
        UInt32 y = 0;
    };

    [[nodiscard]] VirtualShadowPhysicalPageOrigin GetVirtualShadowPhysicalPageOrigin(UInt32 physicalPageIndex, UInt32 atlasExtent) noexcept;
    [[nodiscard]] UInt32 GetVirtualShadowPhysicalPageCapacity(UInt32 atlasExtent) noexcept;

    class VirtualShadowPageCache
    {
    public:
        explicit VirtualShadowPageCache(UInt32 capacity);

        void BeginFrame(UInt64 frameIndex) noexcept;
        [[nodiscard]] std::optional<UInt32> Request(VirtualShadowPageRequest request);
        [[nodiscard]] VirtualShadowRequestResolution ResolveRequests(std::span<const VirtualShadowPageRequest> requests);
        void MarkRendered(std::span<const VirtualShadowPageKey> keys);
        [[nodiscard]] bool Invalidate(VirtualShadowPageKey key) noexcept;
        void InvalidateAll() noexcept;

        [[nodiscard]] bool Contains(VirtualShadowPageKey key) const;
        [[nodiscard]] std::optional<UInt32> FindPhysicalPage(VirtualShadowPageKey key) const;
        [[nodiscard]] UInt32 GetCapacity() const noexcept;
        [[nodiscard]] UInt32 GetCachedPageCount() const noexcept;
        [[nodiscard]] UInt32 GetResidentPageCount() const noexcept;
        [[nodiscard]] UInt32 GetDirtyPageCount() const noexcept;
        [[nodiscard]] std::span<const VirtualShadowPhysicalPage> GetPhysicalPages() const noexcept;
        [[nodiscard]] std::vector<UInt32> GetDirtyPhysicalPageIndices() const;
        [[nodiscard]] VirtualShadowPageTable BuildResidentPageTable() const;

    private:
        [[nodiscard]] std::optional<UInt32> FindEvictionCandidate() const noexcept;
        [[nodiscard]] UInt32 AcquirePhysicalPage();

    private:
        std::vector<VirtualShadowPhysicalPage> pages_;
        std::vector<UInt32> freePages_;
        std::unordered_map<VirtualShadowPageKey, UInt32, VirtualShadowPageKeyHash> mappings_;
        UInt64 currentFrame_ = 0;
        UInt32 evictionsThisFrame_ = 0;
    };
} // namespace ve
