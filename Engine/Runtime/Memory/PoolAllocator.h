#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"

#include <cstddef>
#include <vector>

namespace ve
{
struct PoolAllocatorDesc
{
    SizeT blockSize = 0;
    SizeT blockCount = 0;
    SizeT alignment = alignof(std::max_align_t);
};

struct PoolAllocatorStats
{
    SizeT blockSize = 0;
    SizeT blockStride = 0;
    SizeT blockCount = 0;
    SizeT allocatedBlockCount = 0;
    SizeT highWaterMark = 0;
    SizeT totalBytes = 0;
};

class PoolAllocator : public NonMovable
{
public:
    PoolAllocator() = default;
    ~PoolAllocator();

    [[nodiscard]] Result<void> Initialize(const PoolAllocatorDesc& desc);
    void Shutdown() noexcept;

    [[nodiscard]] void* Allocate() noexcept;
    [[nodiscard]] bool Free(void* pointer) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool Owns(const void* pointer) const noexcept;
    [[nodiscard]] PoolAllocatorStats GetStats() const noexcept;

private:
    struct FreeBlock
    {
        FreeBlock* next = nullptr;
    };

    void BuildFreeList() noexcept;
    [[nodiscard]] SizeT GetBlockIndex(const void* pointer) const noexcept;

private:
    std::byte* memory_ = nullptr;
    FreeBlock* freeList_ = nullptr;
    std::vector<UInt8> allocatedBlocks_;
    SizeT blockSize_ = 0;
    SizeT blockStride_ = 0;
    SizeT blockCount_ = 0;
    SizeT alignment_ = 0;
    SizeT allocatedBlockCount_ = 0;
    SizeT highWaterMark_ = 0;
    SizeT totalBytes_ = 0;
};
}
