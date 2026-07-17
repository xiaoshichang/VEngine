#include "Engine/Runtime/Render/Renderer/RenderQueue.h"

#include "Engine/Runtime/Core/Error.h"

#include <string>

namespace ve
{
    const char* ToString(RenderQueue renderQueue) noexcept
    {
        switch (renderQueue)
        {
        case RenderQueue::Opaque:
            return "Opaque";
        case RenderQueue::Transparent:
            return "Transparent";
        default:
            return "Unknown";
        }
    }

    Result<RenderQueue> ParseRenderQueue(std::string_view value)
    {
        if (value == "Opaque")
        {
            return Result<RenderQueue>::Success(RenderQueue::Opaque);
        }

        if (value == "Transparent")
        {
            return Result<RenderQueue>::Success(RenderQueue::Transparent);
        }

        return Result<RenderQueue>::Failure(Error(ErrorCode::InvalidArgument, "Unknown material renderQueue '" + std::string(value) + "'."));
    }
} // namespace ve
