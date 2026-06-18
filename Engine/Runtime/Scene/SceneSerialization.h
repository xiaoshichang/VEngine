#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"

#include <string>
#include <string_view>

namespace ve
{
    class Scene;

    class SceneSerialization
    {
    public:
        [[nodiscard]] static Result<std::string> SaveToString(const Scene& scene);
        [[nodiscard]] static ErrorCode LoadFromString(Scene& scene, std::string_view text);
    };
} // namespace ve
