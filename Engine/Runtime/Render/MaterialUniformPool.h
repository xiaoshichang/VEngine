#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>
#include <unordered_set>
#include <vector>

namespace ve
{
    struct MaterialUniformAllocation
    {
        rhi::RhiBuffer* buffer = nullptr;
        UInt64 offset = 0;
        UInt64 size = 0;
        UInt64 handle = 0;
    };

    /// Owns persistent CPU-visible uniform pages and stable slots used by render-thread material resources.
    class MaterialUniformPool final : public NonCopyable
    {
    public:
        static constexpr UInt64 PageSize = 64 * 1024;
        static constexpr UInt64 AllocationAlignment = 256;

        void Initialize(rhi::RhiDevice& device) noexcept;
        void Shutdown() noexcept;
        [[nodiscard]] MaterialUniformAllocation Allocate(UInt64 size);
        [[nodiscard]] bool IsValid(const MaterialUniformAllocation& allocation) const noexcept;
        void Update(const MaterialUniformAllocation& allocation, const void* data, UInt64 size);
        void Release(MaterialUniformAllocation& allocation);

    private:
        struct Page
        {
            std::unique_ptr<rhi::RhiBuffer> buffer;
            UInt64 cursor = 0;
            bool hasBeenWritten = false;
        };

        struct FreeSlot
        {
            SizeT pageIndex = 0;
            UInt64 offset = 0;
            UInt64 size = 0;
        };

        [[nodiscard]] MaterialUniformAllocation AllocateFromPage(SizeT pageIndex, UInt64 offset, UInt64 size);
        [[nodiscard]] SizeT FindPageIndex(const rhi::RhiBuffer* buffer) const noexcept;

        rhi::RhiDevice* device_ = nullptr;
        std::vector<Page> pages_;
        std::vector<FreeSlot> freeSlots_;
        std::unordered_set<UInt64> activeHandles_;
        UInt64 nextHandle_ = 1;
    };
} // namespace ve
