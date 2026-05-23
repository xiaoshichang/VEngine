#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Memory/PoolAllocator.h"

#include <cstdint>
#include <iostream>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }

    return condition;
}

bool ExpectOk(ve::ErrorCode result, const char* message)
{
    if (result == ve::ErrorCode::None)
    {
        return true;
    }

    std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
    return false;
}

bool TestInitializeAndStats()
{
    bool passed = true;

    ve::PoolAllocator allocator;
    passed &= ExpectOk(
        allocator.Initialize(ve::PoolAllocatorDesc{32, 4, 16}),
        "PoolAllocator should initialize with a valid descriptor");

    const ve::PoolAllocatorStats stats = allocator.GetStats();
    passed &= Expect(allocator.IsInitialized(), "PoolAllocator should report initialized state");
    passed &= Expect(stats.blockSize == 32, "PoolAllocator should preserve requested block size");
    passed &= Expect(stats.blockCount == 4, "PoolAllocator should preserve block count");
    passed &= Expect(stats.blockStride >= 32, "PoolAllocator stride should cover block size");
    passed &= Expect((stats.blockStride % 16) == 0, "PoolAllocator stride should honor requested alignment");
    passed &= Expect(stats.allocatedBlockCount == 0, "PoolAllocator should start with no allocated blocks");
    passed &= Expect(stats.highWaterMark == 0, "PoolAllocator high water mark should start at zero");
    passed &=
        Expect(stats.totalBytes == stats.blockStride * stats.blockCount, "PoolAllocator total bytes should match");

    allocator.Shutdown();
    passed &= Expect(!allocator.IsInitialized(), "PoolAllocator should report shutdown state");
    passed &= Expect(allocator.GetStats().totalBytes == 0, "PoolAllocator stats should clear on shutdown");

    return passed;
}

bool TestAllocateUntilFullAndReuse()
{
    bool passed = true;

    ve::PoolAllocator allocator;
    passed &= ExpectOk(
        allocator.Initialize(ve::PoolAllocatorDesc{sizeof(int), 2, alignof(int)}),
        "PoolAllocator should initialize for small blocks");

    void* first = allocator.Allocate();
    void* second = allocator.Allocate();
    void* third = allocator.Allocate();

    passed &= Expect(first != nullptr, "First allocation should succeed");
    passed &= Expect(second != nullptr, "Second allocation should succeed");
    passed &= Expect(first != second, "Allocated blocks should be distinct");
    passed &= Expect(third == nullptr, "Allocation should return nullptr when the pool is full");
    passed &= Expect(allocator.Owns(first), "PoolAllocator should own first block");
    passed &= Expect(allocator.Owns(second), "PoolAllocator should own second block");

    ve::PoolAllocatorStats fullStats = allocator.GetStats();
    passed &= Expect(fullStats.allocatedBlockCount == 2, "Stats should track allocated block count");
    passed &= Expect(fullStats.highWaterMark == 2, "Stats should track high water mark");

    passed &= Expect(allocator.Free(first), "Free should accept an allocated block");
    void* reused = allocator.Allocate();
    passed &= Expect(reused == first, "Freed block should be reusable");

    passed &= Expect(allocator.Free(second), "Free should accept second block");
    passed &= Expect(allocator.Free(reused), "Free should accept reused block");
    passed &= Expect(allocator.GetStats().allocatedBlockCount == 0, "Stats should return to zero after frees");

    return passed;
}

bool TestAlignment()
{
    bool passed = true;

    ve::PoolAllocator allocator;
    passed &= ExpectOk(
        allocator.Initialize(ve::PoolAllocatorDesc{24, 3, 32}),
        "PoolAllocator should initialize with a larger alignment");

    void* first = allocator.Allocate();
    void* second = allocator.Allocate();

    passed &= Expect(first != nullptr, "Aligned first allocation should succeed");
    passed &= Expect(second != nullptr, "Aligned second allocation should succeed");

    const auto firstAddress = reinterpret_cast<std::uintptr_t>(first);
    const auto secondAddress = reinterpret_cast<std::uintptr_t>(second);

    passed &= Expect((firstAddress % 32) == 0, "First allocation should satisfy alignment");
    passed &= Expect((secondAddress % 32) == 0, "Second allocation should satisfy alignment");

    return passed;
}

bool TestInvalidInputsAndFrees()
{
    bool passed = true;

    ve::PoolAllocator allocator;

    const ve::ErrorCode zeroBlockSize = allocator.Initialize(ve::PoolAllocatorDesc{0, 1, 8});
    passed &= Expect(zeroBlockSize != ve::ErrorCode::None, "Zero block size should fail");
    passed &= Expect(zeroBlockSize == ve::ErrorCode::InvalidArgument, "Zero block size should return InvalidArgument");

    const ve::ErrorCode zeroBlockCount = allocator.Initialize(ve::PoolAllocatorDesc{8, 0, 8});
    passed &= Expect(zeroBlockCount != ve::ErrorCode::None, "Zero block count should fail");
    passed &= Expect(zeroBlockCount == ve::ErrorCode::InvalidArgument, "Zero block count should return InvalidArgument");

    const ve::ErrorCode badAlignment = allocator.Initialize(ve::PoolAllocatorDesc{8, 1, 3});
    passed &= Expect(badAlignment != ve::ErrorCode::None, "Non-power-of-two alignment should fail");
    passed &= Expect(badAlignment == ve::ErrorCode::InvalidArgument, "Bad alignment should return InvalidArgument");

    passed &= ExpectOk(
        allocator.Initialize(ve::PoolAllocatorDesc{16, 1, 8}),
        "PoolAllocator should initialize after rejected descriptors");

    int foreignValue = 0;
    void* block = allocator.Allocate();

    passed &= Expect(!allocator.Free(nullptr), "Free should reject nullptr");
    passed &= Expect(!allocator.Free(&foreignValue), "Free should reject foreign pointers");
    passed &= Expect(block != nullptr, "Allocation should succeed before free validation");
    passed &= Expect(allocator.Free(block), "Free should accept allocated block");
    passed &= Expect(!allocator.Free(block), "Free should reject double free");

    return passed;
}
}

int main()
{
    bool passed = true;

    passed &= TestInitializeAndStats();
    passed &= TestAllocateUntilFullAndReuse();
    passed &= TestAlignment();
    passed &= TestInvalidInputsAndFrees();

    if (passed)
    {
        std::cout << "VEngineMemoryTests passed" << '\n';
        return 0;
    }

    return 1;
}
