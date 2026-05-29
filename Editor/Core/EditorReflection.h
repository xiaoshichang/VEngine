#pragma once

#include "Engine/Runtime/Reflection/ReflectionRegistry.h"

#include <string_view>

namespace ve
{
    class GameObject;

    [[nodiscard]] bool IsEditorEditableReflectedPropertyType(ReflectedPropertyType type) noexcept;
    [[nodiscard]] bool IsSingleInstanceEditorComponent(std::string_view typeName) noexcept;
    [[nodiscard]] const ReflectedTypeInfo*
    FindReflectedTypeForComponent(const ReflectionRegistry& reflectionRegistry, const Component& component);
    [[nodiscard]] bool HasReflectedComponentType(const ReflectionRegistry& reflectionRegistry,
                                                 const GameObject& gameObject,
                                                 std::string_view typeName);
    [[nodiscard]] bool CanAddReflectedComponentToGameObject(const ReflectionRegistry& reflectionRegistry,
                                                            const GameObject& gameObject,
                                                            const ReflectedTypeInfo& typeInfo);
} // namespace ve
