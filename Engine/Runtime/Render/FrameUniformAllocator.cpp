#include "Engine/Runtime/Render/FrameUniformAllocator.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

namespace ve
{
    namespace
    {
        [[nodiscard]] constexpr UInt64 AlignUp(UInt64 value, UInt64 alignment) noexcept
        {
            return (value + alignment - 1) & ~(alignment - 1);
        }
    } // namespace

    void FrameUniformAllocator::Initialize(rhi::RhiDevice& device) noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ == nullptr);
        device_ = &device;
    }

    void FrameUniformAllocator::Reset() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        for (Page& page : pages_)
        {
            page.cursor = 0;
        }
    }

    void FrameUniformAllocator::Shutdown() noexcept
    {
        VE_ASSERT_RENDER_THREAD();
        pages_.clear();
        device_ = nullptr;
    }

    UniformBufferAllocation FrameUniformAllocator::Upload(const void* data, UInt64 size)
    {
        VE_ASSERT_RENDER_THREAD();
        VE_ASSERT(device_ != nullptr);
        VE_ASSERT(data != nullptr);
        VE_ASSERT(size > 0);

        const UInt64 alignedSize = AlignUp(size, AllocationAlignment);
        VE_ASSERT_MESSAGE(alignedSize <= PageSize, "A frame uniform allocation cannot exceed one allocator page.");
        Page& page = AcquirePage(alignedSize);
        const UInt64 offset = page.cursor;
        const rhi::RhiBufferUpdateMode updateMode = offset == 0 ? rhi::RhiBufferUpdateMode::Discard : rhi::RhiBufferUpdateMode::NoOverwrite;
        device_->UpdateBuffer(*page.buffer, offset, data, size, updateMode);
        page.cursor += alignedSize;
        return UniformBufferAllocation{page.buffer.get(), offset, size};
    }

    FrameUniformAllocator::Page& FrameUniformAllocator::AcquirePage(UInt64 alignedSize)
    {
        for (Page& page : pages_)
        {
            if (page.cursor + alignedSize <= PageSize)
            {
                return page;
            }
        }

        rhi::RhiBufferDesc desc = {};
        desc.size = PageSize;
        desc.usage = rhi::RhiBufferUsage::Uniform;
        desc.memoryUsage = rhi::RhiBufferMemoryUsage::CpuToGpu;
        desc.debugName = "FrameUniformAllocatorPage";

        Page page = {};
        page.buffer = device_->CreateBuffer(desc);
        VE_ASSERT_MESSAGE(page.buffer != nullptr, "FrameUniformAllocator failed to create a uniform page.");
        pages_.push_back(std::move(page));
        return pages_.back();
    }
} // namespace ve
