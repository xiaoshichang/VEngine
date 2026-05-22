#pragma once

#include "Engine/Runtime/Core/Types.h"

#include <atomic>

namespace ve
{
/// Engine-owned alias for std::memory_order.
///
/// Threading code should make memory ordering visible at call sites instead of hiding it behind implicit defaults when
/// ordering affects correctness.
using MemoryOrder = std::memory_order;

/// Engine-owned atomic wrapper alias.
///
/// This alias intentionally keeps std::atomic behavior while giving engine modules a stable VEngine spelling. Lock-free
/// code must still document the reason for each acquire, release, relaxed, or sequentially consistent operation.
template <typename T>
using Atomic = std::atomic<T>;

/// Atomic boolean value.
using AtomicBool = Atomic<bool>;

/// Atomic signed 32-bit integer.
using AtomicInt32 = Atomic<Int32>;

/// Atomic unsigned 32-bit integer.
using AtomicUInt32 = Atomic<UInt32>;

/// Atomic size value used for counters and indices.
using AtomicSize = Atomic<SizeT>;
}
