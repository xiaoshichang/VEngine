#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h"

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] bool TryMultiply(UInt64 left, UInt64 right, UInt64& result) noexcept
        {
            if (left != 0 && right > std::numeric_limits<UInt64>::max() / left)
            {
                return false;
            }

            result = left * right;
            return true;
        }

        [[nodiscard]] VirtualShadowSceneCacheDesc NormalizeSceneCacheDesc(VirtualShadowSceneCacheDesc desc) noexcept
        {
            if (desc.maximumViewCount > VirtualShadowMaximumViewID)
            {
                desc.maximumViewCount = 0;
            }
            return desc;
        }

        [[nodiscard]] VirtualShadowPageTableSlice BuildSlice(UInt32 index) noexcept
        {
            VirtualShadowPageTableSlice slice;
            UInt64 pageMarksOffset = 0;
            UInt64 pageTableOffset = 0;
            UInt64 requestListOffset = 0;
            UInt64 requestCountsOffset = 0;
            if (!TryMultiply(index, VirtualShadowLogicalPageBufferSize, pageMarksOffset) ||
                !TryMultiply(index, VirtualShadowLogicalPageBufferSize, pageTableOffset) ||
                !TryMultiply(index, VirtualShadowRequestListSliceSize, requestListOffset) ||
                !TryMultiply(index, VirtualShadowRequestCountBufferSize, requestCountsOffset))
            {
                return slice;
            }

            slice.index = index;
            slice.pageMarksOffset = pageMarksOffset;
            slice.pageTableOffset = pageTableOffset;
            slice.requestListOffset = requestListOffset;
            slice.requestCountsOffset = requestCountsOffset;
            return slice;
        }

        [[nodiscard]] std::unique_ptr<rhi::RhiBuffer> CreateStorageBuffer(rhi::RhiDevice& device, UInt64 size, UInt32 stride, const char* debugName)
        {
            rhi::RhiBufferDesc desc = {};
            desc.size = size;
            desc.usage = rhi::RhiBufferUsage::Storage;
            desc.memoryUsage = rhi::RhiBufferMemoryUsage::GpuOnly;
            desc.structureStride = stride;
            desc.debugName = debugName;
            return device.CreateBuffer(desc);
        }
    } // namespace

    bool VirtualShadowPageTableSlice::IsValid() const noexcept
    {
        return index != std::numeric_limits<UInt32>::max();
    }

    VirtualShadowPageTableSliceAllocator::VirtualShadowPageTableSliceAllocator(UInt32 maximumViewCount)
        : maximumViewCount_(maximumViewCount <= VirtualShadowMaximumViewID ? maximumViewCount : 0)
    {
    }

    VirtualShadowPageTableSlice VirtualShadowPageTableSliceAllocator::Acquire(UInt32 viewID)
    {
        if (viewID == InvalidVirtualShadowViewID || viewID > VirtualShadowMaximumViewID)
        {
            return {};
        }

        for (UInt32 index = 0; index < slotViewIDs_.size(); ++index)
        {
            if (slotViewIDs_[index] == viewID)
            {
                return BuildSlice(index);
            }
        }

        for (UInt32 index = 0; index < slotViewIDs_.size(); ++index)
        {
            if (slotViewIDs_[index] == InvalidVirtualShadowViewID)
            {
                const VirtualShadowPageTableSlice slice = BuildSlice(index);
                if (slice.IsValid())
                {
                    slotViewIDs_[index] = viewID;
                }
                return slice;
            }
        }

        if (slotViewIDs_.size() >= maximumViewCount_)
        {
            return {};
        }

        const UInt32 index = static_cast<UInt32>(slotViewIDs_.size());
        const VirtualShadowPageTableSlice slice = BuildSlice(index);
        if (!slice.IsValid())
        {
            return {};
        }

        slotViewIDs_.push_back(viewID);
        return slice;
    }

    void VirtualShadowPageTableSliceAllocator::Release(UInt32 viewID) noexcept
    {
        if (viewID == InvalidVirtualShadowViewID || viewID > VirtualShadowMaximumViewID)
        {
            return;
        }

        for (UInt32& allocatedViewID : slotViewIDs_)
        {
            if (allocatedViewID == viewID)
            {
                allocatedViewID = InvalidVirtualShadowViewID;
                return;
            }
        }
    }

    UInt32 VirtualShadowPageTableSliceAllocator::GetCapacity() const noexcept
    {
        return maximumViewCount_;
    }

    VirtualShadowSceneCache::VirtualShadowSceneCache(VirtualShadowSceneCacheDesc desc)
        : desc_(NormalizeSceneCacheDesc(desc))
        , sliceAllocator_(desc_.maximumViewCount)
    {
    }

    VirtualShadowSceneCache::~VirtualShadowSceneCache() = default;

    void VirtualShadowSceneCache::EnsureResources(rhi::RhiDevice& device)
    {
        const UInt64 pagesPerAxis = desc_.atlasExtent / VirtualShadowPhysicalPageSize;
        UInt64 uncappedPhysicalPageCapacity = 0;
        UInt64 pageBufferSize = 0;
        UInt64 requestListBufferSize = 0;
        UInt64 requestCountsBufferSize = 0;
        UInt64 physicalPagesBufferSize = 0;
        if (pagesPerAxis == 0 || desc_.maximumViewCount == 0 || !TryMultiply(pagesPerAxis, pagesPerAxis, uncappedPhysicalPageCapacity) ||
            uncappedPhysicalPageCapacity == 0 || !TryMultiply(desc_.maximumViewCount, VirtualShadowLogicalPageBufferSize, pageBufferSize) ||
            !TryMultiply(desc_.maximumViewCount, VirtualShadowRequestListSliceSize, requestListBufferSize) ||
            !TryMultiply(desc_.maximumViewCount, VirtualShadowRequestCountBufferSize, requestCountsBufferSize))
        {
            ReleaseResources();
            FailVirtualShadow("VSM scene-cache resource sizes are invalid.");
        }

        const UInt64 cappedPhysicalPageCapacity = std::min(uncappedPhysicalPageCapacity, static_cast<UInt64>(VirtualShadowMaxPhysicalPageCount));
        if (cappedPhysicalPageCapacity == 0 || cappedPhysicalPageCapacity > std::numeric_limits<UInt32>::max() ||
            !TryMultiply(cappedPhysicalPageCapacity, sizeof(VirtualShadowGpuPhysicalPage), physicalPagesBufferSize))
        {
            ReleaseResources();
            FailVirtualShadow("VSM physical-page capacity is invalid.");
        }
        const UInt32 physicalPageCapacity = static_cast<UInt32>(cappedPhysicalPageCapacity);

        const bool hasCompleteResources = resourceDevice_ == &device && atlasTexture_ != nullptr && pageMarksBuffer_ != nullptr &&
                                          pageTableBuffer_ != nullptr && requestListBuffer_ != nullptr && requestCountsBuffer_ != nullptr &&
                                          physicalPagesBuffer_ != nullptr && statisticsBuffer_ != nullptr && physicalPageCapacity_ == physicalPageCapacity;
        if (hasCompleteResources)
        {
            return;
        }

        ReleaseResources();

        rhi::RhiTextureDesc atlasDesc = {};
        atlasDesc.width = desc_.atlasExtent;
        atlasDesc.height = desc_.atlasExtent;
        atlasDesc.format = rhi::RhiFormat::R32Uint;
        atlasDesc.usage =
            static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::Sampled) | static_cast<UInt32>(rhi::RhiTextureUsage::Storage));
        atlasDesc.debugName = "VirtualShadowSceneAtlas";
        std::unique_ptr<rhi::RhiTexture> atlasTexture = device.CreateTexture(atlasDesc);
        if (atlasTexture == nullptr)
        {
            FailVirtualShadow("VSM failed to create the R32Uint sampled-storage scene atlas.");
        }

        std::unique_ptr<rhi::RhiBuffer> pageMarksBuffer = CreateStorageBuffer(device, pageBufferSize, sizeof(UInt32), "VirtualShadowScenePageMarks");
        if (pageMarksBuffer == nullptr)
        {
            FailVirtualShadow("VSM failed to create the page-marks buffer.");
        }

        std::unique_ptr<rhi::RhiBuffer> pageTableBuffer = CreateStorageBuffer(device, pageBufferSize, sizeof(UInt32), "VirtualShadowSceneDensePageTable");
        if (pageTableBuffer == nullptr)
        {
            FailVirtualShadow("VSM failed to create the page-table buffer.");
        }

        std::unique_ptr<rhi::RhiBuffer> requestListBuffer = CreateStorageBuffer(device, requestListBufferSize, sizeof(UInt32), "VirtualShadowSceneRequestList");
        if (requestListBuffer == nullptr)
        {
            FailVirtualShadow("VSM failed to create the request-list buffer.");
        }

        std::unique_ptr<rhi::RhiBuffer> requestCountsBuffer =
            CreateStorageBuffer(device, requestCountsBufferSize, sizeof(UInt32), "VirtualShadowSceneRequestCounts");
        if (requestCountsBuffer == nullptr)
        {
            FailVirtualShadow("VSM failed to create the request-count buffer.");
        }

        std::unique_ptr<rhi::RhiBuffer> physicalPagesBuffer =
            CreateStorageBuffer(device, physicalPagesBufferSize, sizeof(VirtualShadowGpuPhysicalPage), "VirtualShadowScenePhysicalPages");
        if (physicalPagesBuffer == nullptr)
        {
            FailVirtualShadow("VSM failed to create the physical-page metadata buffer.");
        }

        std::unique_ptr<rhi::RhiBuffer> statisticsBuffer =
            CreateStorageBuffer(device, sizeof(VirtualShadowGpuStatistics), sizeof(UInt32), "VirtualShadowSceneStatistics");
        if (statisticsBuffer == nullptr)
        {
            FailVirtualShadow("VSM failed to create the statistics buffer.");
        }

        atlasTexture_ = std::move(atlasTexture);
        pageMarksBuffer_ = std::move(pageMarksBuffer);
        pageTableBuffer_ = std::move(pageTableBuffer);
        requestListBuffer_ = std::move(requestListBuffer);
        requestCountsBuffer_ = std::move(requestCountsBuffer);
        physicalPagesBuffer_ = std::move(physicalPagesBuffer);
        statisticsBuffer_ = std::move(statisticsBuffer);
        resourceDevice_ = &device;
        physicalPageCapacity_ = physicalPageCapacity;
        resetSceneCachePending_ = true;
    }

    const VirtualShadowSceneCacheDesc& VirtualShadowSceneCache::GetDesc() const noexcept
    {
        return desc_;
    }

    UInt32 VirtualShadowSceneCache::GetPhysicalPageCapacity() const noexcept
    {
        return physicalPageCapacity_;
    }

    VirtualShadowPageTableSliceAllocator& VirtualShadowSceneCache::GetSliceAllocator() noexcept
    {
        return sliceAllocator_;
    }

    const VirtualShadowPageTableSliceAllocator& VirtualShadowSceneCache::GetSliceAllocator() const noexcept
    {
        return sliceAllocator_;
    }

    rhi::RhiTexture* VirtualShadowSceneCache::GetAtlasTexture() noexcept
    {
        return atlasTexture_.get();
    }

    const rhi::RhiTexture* VirtualShadowSceneCache::GetAtlasTexture() const noexcept
    {
        return atlasTexture_.get();
    }

    rhi::RhiBuffer* VirtualShadowSceneCache::GetPageMarksBuffer() noexcept
    {
        return pageMarksBuffer_.get();
    }

    const rhi::RhiBuffer* VirtualShadowSceneCache::GetPageMarksBuffer() const noexcept
    {
        return pageMarksBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowSceneCache::GetPageTableBuffer() noexcept
    {
        return pageTableBuffer_.get();
    }

    const rhi::RhiBuffer* VirtualShadowSceneCache::GetPageTableBuffer() const noexcept
    {
        return pageTableBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowSceneCache::GetRequestListBuffer() noexcept
    {
        return requestListBuffer_.get();
    }

    const rhi::RhiBuffer* VirtualShadowSceneCache::GetRequestListBuffer() const noexcept
    {
        return requestListBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowSceneCache::GetRequestCountsBuffer() noexcept
    {
        return requestCountsBuffer_.get();
    }

    const rhi::RhiBuffer* VirtualShadowSceneCache::GetRequestCountsBuffer() const noexcept
    {
        return requestCountsBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowSceneCache::GetPhysicalPagesBuffer() noexcept
    {
        return physicalPagesBuffer_.get();
    }

    const rhi::RhiBuffer* VirtualShadowSceneCache::GetPhysicalPagesBuffer() const noexcept
    {
        return physicalPagesBuffer_.get();
    }

    rhi::RhiBuffer* VirtualShadowSceneCache::GetStatisticsBuffer() noexcept
    {
        return statisticsBuffer_.get();
    }

    const rhi::RhiBuffer* VirtualShadowSceneCache::GetStatisticsBuffer() const noexcept
    {
        return statisticsBuffer_.get();
    }

    void VirtualShadowSceneCache::RequestSceneCacheReset() noexcept
    {
        resetSceneCachePending_ = true;
    }

    bool VirtualShadowSceneCache::IsSceneCacheResetPending() const noexcept
    {
        return resetSceneCachePending_;
    }

    bool VirtualShadowSceneCache::ConsumeSceneCacheReset() noexcept
    {
        const bool reset = resetSceneCachePending_;
        resetSceneCachePending_ = false;
        return reset;
    }

    void VirtualShadowSceneCache::ReleaseResources() noexcept
    {
        atlasTexture_.reset();
        pageMarksBuffer_.reset();
        pageTableBuffer_.reset();
        requestListBuffer_.reset();
        requestCountsBuffer_.reset();
        physicalPagesBuffer_.reset();
        statisticsBuffer_.reset();
        resourceDevice_ = nullptr;
        physicalPageCapacity_ = 0;
        resetSceneCachePending_ = false;
    }
} // namespace ve
