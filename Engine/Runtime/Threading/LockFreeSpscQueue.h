#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/Atomic.h"

#include <array>
#include <type_traits>
#include <utility>

namespace ve
{
    /// Fixed-capacity single-producer single-consumer lock-free queue.
    ///
    /// This queue is for exactly one producer thread and exactly one consumer thread. Using multiple producers or
    /// multiple consumers is invalid. Capacity is fixed at compile time, and the queue performs no runtime allocation.
    ///
    /// Type requirements for the first implementation:
    /// - T must be default constructible because storage is a fixed array of T.
    /// - T must be copy assignable for TryPush(const T&).
    /// - T must be move assignable for TryPush(T&&).
    ///
    /// Memory-ordering rules:
    /// - Only the producer writes tail_.
    /// - Only the consumer writes head_.
    /// - The producer writes an item before publishing tail_ with release ordering.
    /// - The consumer reads an item only after acquiring tail_.
    /// - The consumer publishes free space by storing head_ with release ordering.
    /// - The producer observes free space by acquiring head_.
    template<typename T, SizeT Capacity>
    class LockFreeSpscQueue : public NonMovable
    {
        static_assert(Capacity > 0, "LockFreeSpscQueue Capacity must be greater than zero.");
        static_assert(std::is_default_constructible_v<T>, "LockFreeSpscQueue requires default constructible T.");
        static_assert(std::is_copy_assignable_v<T>, "LockFreeSpscQueue requires copy assignable T.");
        static_assert(std::is_move_assignable_v<T>, "LockFreeSpscQueue requires move assignable T.");

    public:
        LockFreeSpscQueue() = default;
        ~LockFreeSpscQueue() = default;

        /// Attempts to push a copied value.
        ///
        /// Returns false when the queue is full. This function must be called only by the single producer thread.
        [[nodiscard]] bool TryPush(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
        {
            // The producer is the only writer of tail_, so its current tail can be loaded with relaxed ordering.
            const SizeT tail = tail_.load(std::memory_order_relaxed);
            const SizeT nextTail = NextIndex(tail);

            // Acquire pairs with the consumer's release store to head_ so the producer observes consumed slots.
            if (nextTail == head_.load(std::memory_order_acquire))
            {
                return false;
            }

            storage_[tail] = value;

            // Release publishes the item write before the consumer observes the updated tail_.
            tail_.store(nextTail, std::memory_order_release);
            return true;
        }

        /// Attempts to push a moved value.
        ///
        /// Returns false when the queue is full. This function must be called only by the single producer thread.
        [[nodiscard]] bool TryPush(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            // The producer is the only writer of tail_, so its current tail can be loaded with relaxed ordering.
            const SizeT tail = tail_.load(std::memory_order_relaxed);
            const SizeT nextTail = NextIndex(tail);

            // Acquire pairs with the consumer's release store to head_ so the producer observes consumed slots.
            if (nextTail == head_.load(std::memory_order_acquire))
            {
                return false;
            }

            storage_[tail] = std::move(value);

            // Release publishes the item write before the consumer observes the updated tail_.
            tail_.store(nextTail, std::memory_order_release);
            return true;
        }

        /// Attempts to pop the oldest value into outValue.
        ///
        /// Returns false when the queue is empty. This function must be called only by the single consumer thread.
        [[nodiscard]] bool TryPop(T& outValue) noexcept(std::is_nothrow_move_assignable_v<T>)
        {
            // The consumer is the only writer of head_, so its current head can be loaded with relaxed ordering.
            const SizeT head = head_.load(std::memory_order_relaxed);

            // Acquire pairs with the producer's release store to tail_ so the consumer observes published item writes.
            if (head == tail_.load(std::memory_order_acquire))
            {
                return false;
            }

            outValue = std::move(storage_[head]);

            // Release publishes consumed space before the producer observes the updated head_.
            head_.store(NextIndex(head), std::memory_order_release);
            return true;
        }

        /// Returns a best-effort current empty state.
        ///
        /// The result can become stale immediately when producer or consumer threads continue running.
        [[nodiscard]] bool IsEmpty() const noexcept
        {
            // Acquire both indices so this diagnostic query observes the latest published producer and consumer
            // progress.
            return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
        }

        /// Returns a best-effort current full state.
        ///
        /// The result can become stale immediately when producer or consumer threads continue running.
        [[nodiscard]] bool IsFull() const noexcept
        {
            // Acquire tail_ and head_ so this diagnostic query observes the latest published queue progress.
            const SizeT tail = tail_.load(std::memory_order_acquire);
            return NextIndex(tail) == head_.load(std::memory_order_acquire);
        }

        /// Returns the number of values the queue can hold.
        [[nodiscard]] constexpr SizeT GetCapacity() const noexcept
        {
            return Capacity;
        }

    private:
        [[nodiscard]] static constexpr SizeT NextIndex(SizeT index) noexcept
        {
            return (index + 1) % StorageCount;
        }

    private:
        static constexpr SizeT StorageCount = Capacity + 1;

        std::array<T, StorageCount> storage_{};
        AtomicSize head_{0};
        AtomicSize tail_{0};
    };
} // namespace ve
