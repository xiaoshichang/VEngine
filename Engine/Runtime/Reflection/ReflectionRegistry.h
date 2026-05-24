#pragma once

#include "Engine/Runtime/Core/Types.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace boost::json
{
    class value;
}

namespace ve
{
    class Component;

    enum class ReflectedPropertyType
    {
        Bool,
        Float32,
        String,
        Vector3,
        Vector4,
        Quaternion,
        ResourceId,
        Enum,
    };

    struct ReflectedEnumValue
    {
        std::string name;
        Int32 value = 0;
    };

    struct ReflectedEnumInfo
    {
        std::string name;
        std::vector<ReflectedEnumValue> values;
    };

    struct ReflectedPropertyInfo
    {
        std::string name;
        ReflectedPropertyType type = ReflectedPropertyType::String;
        bool serializable = true;
        bool editorVisible = true;
        std::string enumName;
        std::function<boost::json::value(const Component&)> serialize;
        std::function<void(Component&, const boost::json::value&)> deserialize;
    };

    struct ReflectedTypeInfo
    {
        std::string name;
        std::string baseTypeName;
        std::function<std::unique_ptr<Component>()> componentFactory;
        std::vector<ReflectedPropertyInfo> properties;
    };

    class ReflectionRegistry
    {
    public:
        void RegisterType(ReflectedTypeInfo typeInfo);
        void RegisterEnum(ReflectedEnumInfo enumInfo);

        [[nodiscard]] const ReflectedTypeInfo* FindType(std::string_view name) const noexcept;
        [[nodiscard]] const ReflectedEnumInfo* FindEnum(std::string_view name) const noexcept;
        [[nodiscard]] std::unique_ptr<Component> CreateComponent(std::string_view typeName) const;

        [[nodiscard]] const std::vector<ReflectedTypeInfo>& GetTypes() const noexcept;

    private:
        std::vector<ReflectedTypeInfo> types_;
        std::vector<ReflectedEnumInfo> enums_;
        std::unordered_map<std::string, SizeT> typeLookup_;
        std::unordered_map<std::string, SizeT> enumLookup_;
    };

    void RegisterSceneReflectionTypes(ReflectionRegistry& registry);
} // namespace ve
