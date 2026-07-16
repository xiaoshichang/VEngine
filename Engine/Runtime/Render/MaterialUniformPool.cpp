#include "Engine/Runtime/Render/MaterialUniformPool.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <algorithm>

namespace ve
{
    namespace
    {
        [[nodiscard]] constexpr UInt64 AlignUp(UInt64 value, UInt64 alignment) noexcept
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }
    } // namespace

    void MaterialUniformPool::Initialize(rhi::RhiDevice& device) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ == nullptr);
        device_ = &device;
    }

    void MaterialUniformPool::Shutdown() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        activeHandles_.clear();
        freeSlots_.clear();
        pages_.clear();
        device_ = nullptr;
    }

    MaterialUniformAllocation MaterialUniformPool::Allocate(UInt64 size)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ != nullptr);
        VE_ASSERT(size > 0);
        const UInt64 alignedSize = AlignUp(size, AllocationAlignment);
        VE_ASSERT_MESSAGE(alignedSize <= PageSize, "A material uniform allocation cannot exceed one pool page.");

        const auto freeSlot = std::find_if(freeSlots_.begin(), freeSlots_.end(), [alignedSize](const FreeSlot& slot) { return slot.size == alignedSize; });
        if (freeSlot != freeSlots_.end())
        {
            const SizeT pageIndex = freeSlot->pageIndex;
            const UInt64 offset = freeSlot->offset;
            freeSlots_.erase(freeSlot);
            return AllocateFromPage(pageIndex, offset, size);
        }

        for (SizeT pageIndex = 0; pageIndex < pages_.size(); ++pageIndex)
        {
            Page& page = pages_[pageIndex];
            if (page.cursor + alignedSize <= PageSize)
            {
                const UInt64 offset = page.cursor;
                page.cursor += alignedSize;
                return AllocateFromPage(pageIndex, offset, size);
            }
        }

        rhi::RhiBufferDesc desc = {};
        desc.size = PageSize;
        desc.usage = rhi::RhiBufferUsage::Uniform;
        desc.memoryUsage = rhi::RhiBufferMemoryUsage::CpuToGpu;
        desc.debugName = "MaterialUniformPoolPage";

        Page page = {};
        page.buffer = device_->CreateBuffer(desc);
        VE_ASSERT_MESSAGE(page.buffer != nullptr, "MaterialUniformPool failed to create a uniform page.");
        page.cursor = alignedSize;
        pages_.push_back(std::move(page));
        return AllocateFromPage(pages_.size() - 1, 0, size);
    }

    bool MaterialUniformPool::IsValid(const MaterialUniformAllocation& allocation) const noexcept
    {
        return allocation.buffer != nullptr && activeHandles_.contains(allocation.handle) && FindPageIndex(allocation.buffer) < pages_.size();
    }

    void MaterialUniformPool::Update(const MaterialUniformAllocation& allocation, const void* data, UInt64 size)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ != nullptr);
        VE_ASSERT(allocation.buffer != nullptr);
        VE_ASSERT(IsValid(allocation));
        VE_ASSERT(data != nullptr);
        VE_ASSERT(size == allocation.size);

        const SizeT pageIndex = FindPageIndex(allocation.buffer);
        VE_ASSERT(pageIndex < pages_.size());
        Page& page = pages_[pageIndex];
        const rhi::RhiBufferUpdateMode updateMode = page.hasBeenWritten ? rhi::RhiBufferUpdateMode::NoOverwrite : rhi::RhiBufferUpdateMode::Discard;
        device_->UpdateBuffer(*allocation.buffer, allocation.offset, data, size, updateMode);
        page.hasBeenWritten = true;
    }

    void MaterialUniformPool::Release(MaterialUniformAllocation& allocation)
    {
        VE_ASSERT_RENDER_THREAD();
        if (allocation.buffer == nullptr)
        {
            allocation = {};
            return;
        }

        if (!IsValid(allocation))
        {
            allocation = {};
            return;
        }

        VE_ASSERT(activeHandles_.erase(allocation.handle) == 1);
        const SizeT pageIndex = FindPageIndex(allocation.buffer);
        VE_ASSERT(pageIndex < pages_.size());
        freeSlots_.push_back(FreeSlot{pageIndex, allocation.offset, AlignUp(allocation.size, AllocationAlignment)});
        allocation = {};
    }

    MaterialUniformAllocation MaterialUniformPool::AllocateFromPage(SizeT pageIndex, UInt64 offset, UInt64 size)
    {
        VE_ASSERT(pageIndex < pages_.size());
        const UInt64 handle = nextHandle_++;
        activeHandles_.insert(handle);
        return MaterialUniformAllocation{pages_[pageIndex].buffer.get(), offset, size, handle};
    }

    SizeT MaterialUniformPool::FindPageIndex(const rhi::RhiBuffer* buffer) const noexcept
    {
        for (SizeT pageIndex = 0; pageIndex < pages_.size(); ++pageIndex)
        {
            if (pages_[pageIndex].buffer.get() == buffer)
            {
                return pageIndex;
            }
        }
        return pages_.size();
    }
} // namespace ve
