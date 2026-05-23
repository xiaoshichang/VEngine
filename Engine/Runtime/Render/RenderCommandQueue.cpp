#include "Engine/Runtime/Render/RenderCommandQueue.h"

namespace ve
{
Result<void> RenderCommandQueue::Push(RenderCommand command)
{
    return queue_.Push(std::move(command));
}

std::optional<RenderCommand> RenderCommandQueue::TryPop()
{
    return queue_.TryPop();
}

bool RenderCommandQueue::IsEmptyForConsumer() const noexcept
{
    return queue_.IsEmptyForConsumer();
}

void RenderCommandQueue::ClearForConsumer()
{
    queue_.ClearForConsumer();
}
}
