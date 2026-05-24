#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <cstddef>
#include <vector>

namespace ve
{
    /// Describes fixed-size storage owned by a PoolAllocator.
    ///
    /// blockSize is the minimum payload size returned by each allocation. blockCount is the number of blocks in the
    /// pool. alignment must be a non-zero power of two and is applied to every returned block.
    struct PoolAllocatorDesc
    {
        /// Minimum number of usable bytes per allocation.
        SizeT blockSize = 0;

        /// Number of fixed-size blocks created by Initialize().
        SizeT blockCount = 0;

        /// Alignment of each returned block.
        SizeT alignment = alignof(std::max_align_t);
    };

    /// Snapshot of PoolAllocator capacity and usage.
    ///
    /// allocatedBlockCount reports the current live allocation count. highWaterMark is the maximum live allocation
    /// count observed since the most recent successful Initialize(). totalBytes is the backing storage size owned by
    /// the pool.
    struct PoolAllocatorStats
    {
        /// Requested minimum payload size per block.
        SizeT blockSize = 0;

        /// Actual distance in bytes between adjacent blocks after alignment padding.
        SizeT blockStride = 0;

        /// Total number of blocks in the pool.
        SizeT blockCount = 0;

        /// Number of blocks currently allocated.
        SizeT allocatedBlockCount = 0;

        /// Maximum allocatedBlockCount observed during this allocator lifetime.
        SizeT highWaterMark = 0;

        /// Total backing storage bytes owned by the pool.
        SizeT totalBytes = 0;
    };

    /// Fixed-size block allocator for many objects with the same lifetime-independent size.
    ///
    /// PoolAllocator owns one contiguous backing allocation and divides it into equally sized, aligned blocks.
    /// Allocate() returns one free block or nullptr when the pool is exhausted. Free() returns a block to the pool and
    /// rejects null pointers, foreign pointers, misaligned pointers, and double frees.
    ///
    /// Usage:
    /// ```cpp
    /// PoolAllocator allocator;
    /// ErrorCode result = allocator.Initialize(PoolAllocatorDesc{sizeof(MyObject), 128, alignof(MyObject)});
    /// if (result != ErrorCode::None)
    /// {
    ///     return result;
    /// }
    /// void* memory = allocator.Allocate();
    /// MyObject* object = new (memory) MyObject();
    /// object->~MyObject();
    /// allocator.Free(memory);
    /// allocator.Shutdown();
    /// ```
    ///
    /// PoolAllocator is not thread-safe. Callers must provide external synchronization when the same allocator can be
    /// used from more than one thread.
    class PoolAllocator : public NonMovable
    {
    public:
        PoolAllocator() = default;

        /// Calls Shutdown() so owned storage is released even when the caller forgets explicit teardown.
        ~PoolAllocator();

        /// Allocates backing storage and builds the free list.
        ///
        /// Returns InvalidState when the allocator is already initialized, InvalidArgument for invalid or overflowing
        /// descriptors, and OutOfMemory when backing storage or bookkeeping allocation fails.
        [[nodiscard]] ErrorCode Initialize(const PoolAllocatorDesc& desc);

        /// Releases all backing storage and clears statistics.
        ///
        /// The caller is responsible for destroying any live objects before Shutdown(). This function is safe to call
        /// on an uninitialized allocator.
        void Shutdown() noexcept;

        /// Returns one free block, or nullptr when the allocator is not initialized or the pool is full.
        ///
        /// The returned memory is unconstructed raw storage. Use placement new for objects that require construction.
        [[nodiscard]] void* Allocate() noexcept;

        /// Returns a previously allocated block to the pool.
        ///
        /// Returns false for null, foreign, misaligned, uninitialized, or already-free pointers. Free() does not call
        /// object destructors; callers must destroy constructed objects before returning their storage.
        [[nodiscard]] bool Free(void* pointer) noexcept;

        /// Returns true after Initialize() succeeds and before Shutdown() clears the pool.
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Returns true when pointer is aligned to a block start inside this pool's backing storage.
        ///
        /// This does not imply the block is currently allocated; it only answers ownership of the address range.
        [[nodiscard]] bool Owns(const void* pointer) const noexcept;

        /// Returns a current usage snapshot for diagnostics and tests.
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
} // namespace ve
