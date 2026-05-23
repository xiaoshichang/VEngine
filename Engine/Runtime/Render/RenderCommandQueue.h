#pragma once

#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Threading/LockFreeMpscQueue.h"

#include <optional>

namespace ve
{
/// Render-layer command queue built on the generic lock-free MPSC primitive.
///
/// Multiple engine systems may submit render commands concurrently. The Render Thread is the only consumer. This class
/// intentionally owns render-command naming and payload semantics, while LockFreeMpscQueue owns the lower-level MPSC
/// publication algorithm.
class RenderCommandQueue
{
public:
    /// Appends one command for later Render Thread execution.
    [[nodiscard]] Result<void> Push(RenderCommand command);

    /// Attempts to pop one command on the Render Thread.
    [[nodiscard]] std::optional<RenderCommand> TryPop();

    /// Returns true when the Render Thread cannot currently pop a fully linked command.
    [[nodiscard]] bool IsEmptyForConsumer() const noexcept;

    /// Clears queued commands from the Render Thread after producers have stopped.
    void ClearForConsumer();

private:
    LockFreeMpscQueue<RenderCommand> queue_;
};
}
