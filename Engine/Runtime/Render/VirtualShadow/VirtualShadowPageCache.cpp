#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h"

#include <algorithm>
#include <limits>

namespace ve
{
    VirtualShadowPhysicalPageOrigin GetVirtualShadowPhysicalPageOrigin(UInt32 physicalPageIndex, UInt32 atlasExtent) noexcept
    {
        constexpr UInt32 PageStride = VirtualShadowPageSize + (2u * VirtualShadowPageGutter);
        const UInt32 pagesPerRow = atlasExtent / PageStride;
        if (pagesPerRow == 0)
        {
            return {};
        }

        return VirtualShadowPhysicalPageOrigin{
            ((physicalPageIndex % pagesPerRow) * PageStride) + VirtualShadowPageGutter,
            ((physicalPageIndex / pagesPerRow) * PageStride) + VirtualShadowPageGutter,
        };
    }

    UInt32 GetVirtualShadowPhysicalPageCapacity(UInt32 atlasExtent) noexcept
    {
        constexpr UInt32 PageStride = VirtualShadowPageSize + (2u * VirtualShadowPageGutter);
        const UInt32 pagesPerAxis = atlasExtent / PageStride;
        return pagesPerAxis * pagesPerAxis;
    }

    VirtualShadowPageCache::VirtualShadowPageCache(UInt32 capacity)
        : pages_(capacity)
    {
        freePages_.reserve(capacity);
        mappings_.reserve(capacity);
        for (UInt32 pageIndex = capacity; pageIndex > 0; --pageIndex)
        {
            freePages_.push_back(pageIndex - 1u);
        }
    }

    void VirtualShadowPageCache::BeginFrame(UInt64 frameIndex) noexcept
    {
        currentFrame_ = frameIndex;
        evictionsThisFrame_ = 0;
        for (VirtualShadowPhysicalPage& page : pages_)
        {
            page.pinnedThisFrame = false;
        }
    }

    std::optional<UInt32> VirtualShadowPageCache::Request(VirtualShadowPageRequest request)
    {
        if (!request.key.IsValid())
        {
            return std::nullopt;
        }

        const auto existing = mappings_.find(request.key);
        if (existing != mappings_.end())
        {
            VirtualShadowPhysicalPage& page = pages_[existing->second];
            page.lastUsedFrame = currentFrame_;
            page.pinnedThisFrame = true;
            return existing->second;
        }

        const UInt32 physicalPageIndex = AcquirePhysicalPage();
        if (physicalPageIndex == InvalidVirtualShadowPhysicalPage)
        {
            return std::nullopt;
        }

        VirtualShadowPhysicalPage& page = pages_[physicalPageIndex];
        if (page.state != VirtualShadowPhysicalPageState::Free)
        {
            mappings_.erase(page.key);
            ++evictionsThisFrame_;
        }

        ++page.generation;
        page.state = VirtualShadowPhysicalPageState::ResidentDirty;
        page.key = request.key;
        page.lastUsedFrame = currentFrame_;
        page.pinnedThisFrame = true;
        mappings_.emplace(request.key, physicalPageIndex);
        return physicalPageIndex;
    }

    VirtualShadowRequestResolution VirtualShadowPageCache::ResolveRequests(std::span<const VirtualShadowPageRequest> requests)
    {
        std::unordered_map<VirtualShadowPageKey, UInt32, VirtualShadowPageKeyHash> priorities;
        priorities.reserve(requests.size());
        for (const VirtualShadowPageRequest& request : requests)
        {
            if (!request.key.IsValid())
            {
                continue;
            }
            auto [iterator, inserted] = priorities.emplace(request.key, request.priority);
            if (!inserted)
            {
                iterator->second = std::max(iterator->second, request.priority);
            }
        }

        VirtualShadowRequestResolution result;
        result.requested = static_cast<UInt32>(priorities.size());
        std::vector<VirtualShadowPageRequest> misses;
        misses.reserve(priorities.size());
        for (const auto& [key, priority] : priorities)
        {
            if (Contains(key))
            {
                (void)Request({key, priority});
                ++result.hits;
            }
            else
            {
                misses.push_back({key, priority});
            }
        }

        std::ranges::sort(misses, [](const VirtualShadowPageRequest& left, const VirtualShadowPageRequest& right) {
            if (left.priority != right.priority)
            {
                return left.priority > right.priority;
            }
            if (left.key.key0 != right.key.key0)
            {
                return left.key.key0 < right.key.key0;
            }
            return left.key.key1 < right.key.key1;
        });

        const UInt32 initialEvictions = evictionsThisFrame_;
        for (const VirtualShadowPageRequest& request : misses)
        {
            if (Request(request).has_value())
            {
                ++result.allocated;
            }
            else
            {
                ++result.missing;
            }
        }
        result.evicted = evictionsThisFrame_ - initialEvictions;
        return result;
    }

    void VirtualShadowPageCache::MarkRendered(std::span<const VirtualShadowPageKey> keys)
    {
        for (VirtualShadowPageKey key : keys)
        {
            const auto iterator = mappings_.find(key);
            if (iterator != mappings_.end())
            {
                VirtualShadowPhysicalPage& page = pages_[iterator->second];
                if (page.state == VirtualShadowPhysicalPageState::ResidentDirty)
                {
                    page.state = VirtualShadowPhysicalPageState::ResidentClean;
                }
            }
        }
    }

    bool VirtualShadowPageCache::Invalidate(VirtualShadowPageKey key) noexcept
    {
        const auto iterator = mappings_.find(key);
        if (iterator == mappings_.end())
        {
            return false;
        }

        pages_[iterator->second].state = VirtualShadowPhysicalPageState::ResidentDirty;
        return true;
    }

    void VirtualShadowPageCache::InvalidateAll() noexcept
    {
        for (VirtualShadowPhysicalPage& page : pages_)
        {
            if (page.state != VirtualShadowPhysicalPageState::Free)
            {
                page.state = VirtualShadowPhysicalPageState::ResidentDirty;
            }
        }
    }

    bool VirtualShadowPageCache::Contains(VirtualShadowPageKey key) const
    {
        return mappings_.contains(key);
    }

    std::optional<UInt32> VirtualShadowPageCache::FindPhysicalPage(VirtualShadowPageKey key) const
    {
        const auto iterator = mappings_.find(key);
        return iterator == mappings_.end() ? std::nullopt : std::optional<UInt32>(iterator->second);
    }

    UInt32 VirtualShadowPageCache::GetCapacity() const noexcept
    {
        return static_cast<UInt32>(pages_.size());
    }

    UInt32 VirtualShadowPageCache::GetCachedPageCount() const noexcept
    {
        return static_cast<UInt32>(mappings_.size());
    }

    UInt32 VirtualShadowPageCache::GetResidentPageCount() const noexcept
    {
        return static_cast<UInt32>(std::ranges::count_if(pages_, [](const VirtualShadowPhysicalPage& page) {
            return page.state == VirtualShadowPhysicalPageState::ResidentClean;
        }));
    }

    UInt32 VirtualShadowPageCache::GetDirtyPageCount() const noexcept
    {
        return static_cast<UInt32>(std::ranges::count_if(pages_, [](const VirtualShadowPhysicalPage& page) {
            return page.state == VirtualShadowPhysicalPageState::ResidentDirty;
        }));
    }

    std::span<const VirtualShadowPhysicalPage> VirtualShadowPageCache::GetPhysicalPages() const noexcept
    {
        return pages_;
    }

    std::vector<UInt32> VirtualShadowPageCache::GetDirtyPhysicalPageIndices() const
    {
        std::vector<UInt32> result;
        result.reserve(GetDirtyPageCount());
        for (UInt32 pageIndex = 0; pageIndex < pages_.size(); ++pageIndex)
        {
            if (pages_[pageIndex].state == VirtualShadowPhysicalPageState::ResidentDirty)
            {
                result.push_back(pageIndex);
            }
        }
        return result;
    }

    VirtualShadowPageTable VirtualShadowPageCache::BuildResidentPageTable() const
    {
        VirtualShadowPageTable table;
        for (UInt32 pageIndex = 0; pageIndex < pages_.size(); ++pageIndex)
        {
            if (pages_[pageIndex].state == VirtualShadowPhysicalPageState::ResidentClean)
            {
                (void)table.Insert(pages_[pageIndex].key, pageIndex);
            }
        }
        return table;
    }

    std::optional<UInt32> VirtualShadowPageCache::FindEvictionCandidate() const noexcept
    {
        UInt32 candidate = InvalidVirtualShadowPhysicalPage;
        UInt64 oldestFrame = std::numeric_limits<UInt64>::max();
        for (UInt32 pageIndex = 0; pageIndex < pages_.size(); ++pageIndex)
        {
            const VirtualShadowPhysicalPage& page = pages_[pageIndex];
            if (page.state == VirtualShadowPhysicalPageState::Free || page.pinnedThisFrame)
            {
                continue;
            }
            if (page.lastUsedFrame < oldestFrame || (page.lastUsedFrame == oldestFrame && pageIndex < candidate))
            {
                candidate = pageIndex;
                oldestFrame = page.lastUsedFrame;
            }
        }

        return candidate == InvalidVirtualShadowPhysicalPage ? std::nullopt : std::optional<UInt32>(candidate);
    }

    UInt32 VirtualShadowPageCache::AcquirePhysicalPage()
    {
        if (!freePages_.empty())
        {
            const UInt32 pageIndex = freePages_.back();
            freePages_.pop_back();
            return pageIndex;
        }

        return FindEvictionCandidate().value_or(InvalidVirtualShadowPhysicalPage);
    }
} // namespace ve
