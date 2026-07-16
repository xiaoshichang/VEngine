#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>
#include <vector>

namespace ve
{
    struct UniformBufferAllocation
    {
        rhi::RhiBuffer* buffer = nullptr;
        UInt64 offset = 0;
        UInt64 size = 0;
    };

    /// Reuses CPU-visible uniform-buffer pages for one in-flight frame context.
    class FrameUniformAllocator final : public NonCopyable
    {
    public:
        static constexpr UInt64 PageSize = 64 * 1024;
        static constexpr UInt64 AllocationAlignment = 256;

        void Initialize(rhi::RhiDevice& device) noexcept;
        void Reset() noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] UniformBufferAllocation Upload(const void* data, UInt64 size);

    private:
        struct Page
        {
            std::unique_ptr<rhi::RhiBuffer> buffer;
            UInt64 cursor = 0;
        };

        [[nodiscard]] Page& AcquirePage(UInt64 alignedSize);

        rhi::RhiDevice* device_ = nullptr;
        std::vector<Page> pages_;
    };
} // namespace ve
