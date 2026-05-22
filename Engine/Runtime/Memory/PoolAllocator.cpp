#include "Engine/Runtime/Memory/PoolAllocator.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace ve
{
namespace
{
[[nodiscard]] bool IsPowerOfTwo(SizeT value) noexcept
{
    return value != 0 && (value & (value - 1)) == 0;
}

[[nodiscard]] bool TryAlignUp(SizeT value, SizeT alignment, SizeT& alignedValue) noexcept
{
    const SizeT mask = alignment - 1;
    if (value > std::numeric_limits<SizeT>::max() - mask)
    {
        return false;
    }

    alignedValue = (value + mask) & ~mask;
    return true;
}
}

PoolAllocator::~PoolAllocator()
{
    Shutdown();
}

Result<void> PoolAllocator::Initialize(const PoolAllocatorDesc& desc)
{
    if (IsInitialized())
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidState, "PoolAllocator is already initialized."));
    }

    if (desc.blockSize == 0)
    {
        return Result<void>::Failure(
            Error(ErrorCode::InvalidArgument, "PoolAllocator blockSize must be greater than 0."));
    }

    if (desc.blockCount == 0)
    {
        return Result<void>::Failure(
            Error(ErrorCode::InvalidArgument, "PoolAllocator blockCount must be greater than 0."));
    }

    if (!IsPowerOfTwo(desc.alignment))
    {
        return Result<void>::Failure(
            Error(ErrorCode::InvalidArgument, "PoolAllocator alignment must be a non-zero power of two."));
    }

    const SizeT actualAlignment = std::max(desc.alignment, alignof(FreeBlock));
    const SizeT minimumBlockSize = std::max(desc.blockSize, sizeof(FreeBlock));

    SizeT blockStride = 0;
    if (!TryAlignUp(minimumBlockSize, actualAlignment, blockStride))
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidArgument, "PoolAllocator block stride overflow."));
    }

    if (desc.blockCount > std::numeric_limits<SizeT>::max() / blockStride)
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidArgument, "PoolAllocator total byte size overflow."));
    }

    const SizeT totalBytes = blockStride * desc.blockCount;

    std::vector<UInt8> allocatedBlocks;
    try
    {
        allocatedBlocks.assign(desc.blockCount, 0);
    }
    catch (const std::exception&)
    {
        return Result<void>::Failure(Error(ErrorCode::OutOfMemory, "PoolAllocator allocation state failed."));
    }

    void* memory = ::operator new(totalBytes, std::align_val_t(actualAlignment), std::nothrow);
    if (memory == nullptr)
    {
        return Result<void>::Failure(Error(ErrorCode::OutOfMemory, "PoolAllocator memory allocation failed."));
    }

    memory_ = static_cast<std::byte*>(memory);
    allocatedBlocks_ = std::move(allocatedBlocks);
    blockSize_ = desc.blockSize;
    blockStride_ = blockStride;
    blockCount_ = desc.blockCount;
    alignment_ = actualAlignment;
    allocatedBlockCount_ = 0;
    highWaterMark_ = 0;
    totalBytes_ = totalBytes;

    BuildFreeList();
    return Result<void>::Success();
}

void PoolAllocator::Shutdown() noexcept
{
    if (memory_ != nullptr)
    {
        ::operator delete(memory_, std::align_val_t(alignment_));
    }

    memory_ = nullptr;
    freeList_ = nullptr;
    allocatedBlocks_.clear();
    blockSize_ = 0;
    blockStride_ = 0;
    blockCount_ = 0;
    alignment_ = 0;
    allocatedBlockCount_ = 0;
    highWaterMark_ = 0;
    totalBytes_ = 0;
}

void* PoolAllocator::Allocate() noexcept
{
    if (freeList_ == nullptr)
    {
        return nullptr;
    }

    FreeBlock* block = freeList_;
    freeList_ = block->next;

    const SizeT blockIndex = GetBlockIndex(block);
    allocatedBlocks_[blockIndex] = 1;
    ++allocatedBlockCount_;
    highWaterMark_ = std::max(highWaterMark_, allocatedBlockCount_);

    return block;
}

bool PoolAllocator::Free(void* pointer) noexcept
{
    if (!Owns(pointer))
    {
        return false;
    }

    const SizeT blockIndex = GetBlockIndex(pointer);
    if (allocatedBlocks_[blockIndex] == 0)
    {
        return false;
    }

    FreeBlock* block = ::new (pointer) FreeBlock{freeList_};
    freeList_ = block;

    allocatedBlocks_[blockIndex] = 0;
    --allocatedBlockCount_;
    return true;
}

bool PoolAllocator::IsInitialized() const noexcept
{
    return memory_ != nullptr;
}

bool PoolAllocator::Owns(const void* pointer) const noexcept
{
    if (memory_ == nullptr || pointer == nullptr)
    {
        return false;
    }

    const auto begin = reinterpret_cast<std::uintptr_t>(memory_);
    const auto end = begin + totalBytes_;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);

    return address >= begin && address < end && ((address - begin) % blockStride_) == 0;
}

PoolAllocatorStats PoolAllocator::GetStats() const noexcept
{
    return PoolAllocatorStats{
        blockSize_,
        blockStride_,
        blockCount_,
        allocatedBlockCount_,
        highWaterMark_,
        totalBytes_,
    };
}

void PoolAllocator::BuildFreeList() noexcept
{
    freeList_ = nullptr;

    for (SizeT index = blockCount_; index > 0; --index)
    {
        std::byte* blockMemory = memory_ + ((index - 1) * blockStride_);
        FreeBlock* block = ::new (blockMemory) FreeBlock{freeList_};
        freeList_ = block;
    }
}

SizeT PoolAllocator::GetBlockIndex(const void* pointer) const noexcept
{
    const auto begin = reinterpret_cast<std::uintptr_t>(memory_);
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return static_cast<SizeT>((address - begin) / blockStride_);
}
}
