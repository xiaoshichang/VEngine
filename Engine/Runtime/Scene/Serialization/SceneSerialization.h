#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Scene/Scene.h"

#include <string>

namespace ve
{
    [[nodiscard]] std::string SerializeSceneToJson(const Scene& scene, const ReflectionRegistry& reflectionRegistry);
    [[nodiscard]] ErrorCode
    DeserializeSceneFromJson(Scene& scene, const ReflectionRegistry& reflectionRegistry, std::string_view jsonText);
} // namespace ve
