#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"

#include <limits>
#include <memory>
#include <vector>

namespace ve
{
    namespace rhi
    {
        class RhiBuffer;
        class RhiDevice;
        class RhiTexture;
    } // namespace rhi

    constexpr UInt64 VirtualShadowLogicalPageBufferSize = static_cast<UInt64>(VirtualShadowLogicalPageCount) * sizeof(UInt32);
    constexpr UInt64 VirtualShadowRequestListSliceSize = static_cast<UInt64>(VirtualShadowLogicalPageCount) * sizeof(UInt32);
    constexpr UInt64 VirtualShadowRequestCountBufferSize = static_cast<UInt64>(VirtualShadowClipmapLevelCount) * sizeof(UInt32);

    struct VirtualShadowSceneCacheDesc
    {
        UInt32 atlasExtent = 4096;
        UInt32 maximumViewCount = 8;
    };

    struct VirtualShadowPageTableSlice
    {
        UInt32 index = std::numeric_limits<UInt32>::max();
        UInt64 pageMarksOffset = 0;
        UInt64 pageTableOffset = 0;
        UInt64 requestListOffset = 0;
        UInt64 requestCountsOffset = 0;

        [[nodiscard]] bool IsValid() const noexcept;
    };

    class VirtualShadowPageTableSliceAllocator
    {
    public:
        explicit VirtualShadowPageTableSliceAllocator(UInt32 maximumViewCount);

        [[nodiscard]] VirtualShadowPageTableSlice Acquire(UInt32 viewID);
        void Release(UInt32 viewID) noexcept;
        [[nodiscard]] UInt32 GetCapacity() const noexcept;

    private:
        UInt32 maximumViewCount_ = 0;
        std::vector<UInt32> slotViewIDs_;
    };

    class VirtualShadowSceneCache final : public NonCopyable
    {
    public:
        explicit VirtualShadowSceneCache(VirtualShadowSceneCacheDesc desc = {});
        ~VirtualShadowSceneCache();

        void EnsureResources(rhi::RhiDevice& device);

        [[nodiscard]] const VirtualShadowSceneCacheDesc& GetDesc() const noexcept;
        [[nodiscard]] UInt32 GetPhysicalPageCapacity() const noexcept;
        [[nodiscard]] VirtualShadowPageTableSliceAllocator& GetSliceAllocator() noexcept;
        [[nodiscard]] const VirtualShadowPageTableSliceAllocator& GetSliceAllocator() const noexcept;

        [[nodiscard]] rhi::RhiTexture* GetAtlasTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetAtlasTexture() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetPageMarksBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetPageMarksBuffer() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetPageTableBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetPageTableBuffer() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetRequestListBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetRequestListBuffer() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetRequestCountsBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetRequestCountsBuffer() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetPhysicalPagesBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetPhysicalPagesBuffer() const noexcept;
        [[nodiscard]] rhi::RhiBuffer* GetStatisticsBuffer() noexcept;
        [[nodiscard]] const rhi::RhiBuffer* GetStatisticsBuffer() const noexcept;

        void RequestSceneCacheReset() noexcept;
        [[nodiscard]] bool IsSceneCacheResetPending() const noexcept;
        [[nodiscard]] bool ConsumeSceneCacheReset() noexcept;

    private:
        void ReleaseResources() noexcept;

    private:
        VirtualShadowSceneCacheDesc desc_;
        UInt32 physicalPageCapacity_ = 0;
        rhi::RhiDevice* resourceDevice_ = nullptr;
        std::unique_ptr<rhi::RhiTexture> atlasTexture_;
        std::unique_ptr<rhi::RhiBuffer> pageMarksBuffer_;
        std::unique_ptr<rhi::RhiBuffer> pageTableBuffer_;
        std::unique_ptr<rhi::RhiBuffer> requestListBuffer_;
        std::unique_ptr<rhi::RhiBuffer> requestCountsBuffer_;
        std::unique_ptr<rhi::RhiBuffer> physicalPagesBuffer_;
        std::unique_ptr<rhi::RhiBuffer> statisticsBuffer_;
        VirtualShadowPageTableSliceAllocator sliceAllocator_;
        bool resetSceneCachePending_ = false;
    };
} // namespace ve
