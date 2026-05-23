#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/Atomic.h"

#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

namespace ve
{
/// Lock-free multi-producer single-consumer queue.
///
/// Any number of producer threads may call Push() concurrently. Exactly one consumer thread may call TryPop() and
/// ClearForConsumer(). Destruction or clearing must not race with active producers.
///
/// The queue uses a stub-node linked list:
/// - producers publish nodes with an atomic head exchange;
/// - each producer links the previous head to its node with release ordering;
/// - the single consumer advances tail through published next pointers with acquire ordering.
///
/// Push() allocates one node per value. This keeps the primitive general and simple for first-stage runtime systems.
/// Higher-level queues may add pooling later without changing their public queue semantics.
template <typename T>
class LockFreeMpscQueue : public NonMovable
{
    static_assert(std::is_move_constructible_v<T>, "LockFreeMpscQueue requires move constructible values.");

public:
    LockFreeMpscQueue()
    {
        Node* sentinel = new Node();
        head_.store(sentinel, std::memory_order_relaxed);
        tail_ = sentinel;
    }

    ~LockFreeMpscQueue()
    {
        DeleteAllNodes();
    }

    /// Appends a value to the queue.
    ///
    /// This function is safe to call from multiple producer threads. Returns OutOfMemory when node allocation fails.
    [[nodiscard]] Result<void> Push(T value)
    {
        std::unique_ptr<Node> node;
        try
        {
            node = std::make_unique<Node>(std::move(value));
        }
        catch (const std::bad_alloc&)
        {
            return Result<void>::Failure(Error(ErrorCode::OutOfMemory, "MPSC queue node allocation failed."));
        }

        Node* rawNode = node.get();
        Node* previous = head_.exchange(rawNode, std::memory_order_acq_rel);
        node.release();

        previous->next.store(rawNode, std::memory_order_release);
        return Result<void>::Success();
    }

    /// Attempts to pop the oldest value.
    ///
    /// This function must be called only by the single consumer thread. Returns std::nullopt when no fully linked node
    /// is available.
    [[nodiscard]] std::optional<T> TryPop()
    {
        Node* oldTail = tail_;
        if (oldTail == nullptr)
        {
            return std::nullopt;
        }

        Node* next = oldTail->next.load(std::memory_order_acquire);
        if (next == nullptr)
        {
            return std::nullopt;
        }

        std::optional<T> result(std::move(*next->value));
        next->value.reset();
        tail_ = next;
        oldTail->next.store(nullptr, std::memory_order_relaxed);
        delete oldTail;
        return result;
    }

    /// Returns true when the consumer cannot currently pop a fully linked value.
    ///
    /// This query is intended for the single consumer and shutdown diagnostics. The result can become stale immediately
    /// while producers continue running.
    [[nodiscard]] bool IsEmptyForConsumer() const noexcept
    {
        return tail_ == nullptr || tail_->next.load(std::memory_order_acquire) == nullptr;
    }

    /// Removes any queued values while preserving an empty sentinel node.
    ///
    /// This function must be called only by the single consumer and must not race with active producers.
    void ClearForConsumer()
    {
        while (TryPop())
        {
        }

        if (tail_ != nullptr)
        {
            tail_->value.reset();
        }
    }

private:
    struct Node
    {
        Node() = default;

        explicit Node(T valueIn)
            : value(std::move(valueIn))
        {
        }

        std::optional<T> value;
        Atomic<Node*> next{nullptr};
    };

    void DeleteAllNodes() noexcept
    {
        Node* node = tail_;
        tail_ = nullptr;
        head_.store(nullptr, std::memory_order_relaxed);

        while (node != nullptr)
        {
            Node* next = node->next.load(std::memory_order_relaxed);
            delete node;
            node = next;
        }
    }

    Atomic<Node*> head_{nullptr};
    Node* tail_ = nullptr;
};
}
