#include "Editor/Core/EditorReflection.h"

#include "Engine/Runtime/Scene/GameObject.h"

#include <memory>
#include <typeinfo>

namespace ve
{
    bool IsEditorEditableReflectedPropertyType(ReflectedPropertyType type) noexcept
    {
        switch (type)
        {
        case ReflectedPropertyType::Bool:
        case ReflectedPropertyType::Float32:
        case ReflectedPropertyType::String:
        case ReflectedPropertyType::Vector3:
        case ReflectedPropertyType::Vector4:
        case ReflectedPropertyType::Quaternion:
        case ReflectedPropertyType::ResourceId:
        case ReflectedPropertyType::UInt64:
        case ReflectedPropertyType::Enum:
            return true;
        }

        return false;
    }

    bool IsSingleInstanceEditorComponent(std::string_view typeName) noexcept
    {
        return typeName == "TransformComponent" || typeName == "ColliderComponent";
    }

    const ReflectedTypeInfo*
    FindReflectedTypeForComponent(const ReflectionRegistry& reflectionRegistry, const Component& component)
    {
        for (const ReflectedTypeInfo& typeInfo : reflectionRegistry.GetTypes())
        {
            std::unique_ptr<Component> probe = typeInfo.componentFactory ? typeInfo.componentFactory() : nullptr;
            if (probe != nullptr && typeid(*probe) == typeid(component))
            {
                return &typeInfo;
            }
        }

        return nullptr;
    }

    bool HasReflectedComponentType(const ReflectionRegistry& reflectionRegistry,
                                   const GameObject& gameObject,
                                   std::string_view typeName)
    {
        for (const std::unique_ptr<Component>& component : gameObject.GetComponents())
        {
            const ReflectedTypeInfo* existingType = FindReflectedTypeForComponent(reflectionRegistry, *component);
            if (existingType != nullptr && existingType->name == typeName)
            {
                return true;
            }
        }

        return false;
    }

    bool CanAddReflectedComponentToGameObject(const ReflectionRegistry& reflectionRegistry,
                                              const GameObject& gameObject,
                                              const ReflectedTypeInfo& typeInfo)
    {
        return !IsSingleInstanceEditorComponent(typeInfo.name) ||
               !HasReflectedComponentType(reflectionRegistry, gameObject, typeInfo.name);
    }
} // namespace ve
