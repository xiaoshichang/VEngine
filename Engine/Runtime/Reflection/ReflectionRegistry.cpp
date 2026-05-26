#include "Engine/Runtime/Reflection/ReflectionRegistry.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/RenderComponents.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <boost/json.hpp>

namespace ve
{
    namespace
    {
        using boost::json::array;
        using boost::json::object;
        using boost::json::value;

        [[nodiscard]] value ToJson(const Vector3& vector)
        {
            return array{vector.GetX(), vector.GetY(), vector.GetZ()};
        }

        [[nodiscard]] value ToJson(const Vector4& vector)
        {
            return array{vector.GetX(), vector.GetY(), vector.GetZ(), vector.GetW()};
        }

        [[nodiscard]] value ToJson(const Quaternion& quaternion)
        {
            return array{quaternion.GetX(), quaternion.GetY(), quaternion.GetZ(), quaternion.GetW()};
        }

        [[nodiscard]] Vector3 ToVector3(const value& jsonValue, const Vector3& fallback)
        {
            if (!jsonValue.is_array() || jsonValue.as_array().size() != 3)
            {
                return fallback;
            }

            const array& values = jsonValue.as_array();
            return Vector3(static_cast<Float32>(values[0].as_double()),
                           static_cast<Float32>(values[1].as_double()),
                           static_cast<Float32>(values[2].as_double()));
        }

        [[nodiscard]] Vector4 ToVector4(const value& jsonValue, const Vector4& fallback)
        {
            if (!jsonValue.is_array() || jsonValue.as_array().size() != 4)
            {
                return fallback;
            }

            const array& values = jsonValue.as_array();
            return Vector4(static_cast<Float32>(values[0].as_double()),
                           static_cast<Float32>(values[1].as_double()),
                           static_cast<Float32>(values[2].as_double()),
                           static_cast<Float32>(values[3].as_double()));
        }

        [[nodiscard]] Quaternion ToQuaternion(const value& jsonValue, const Quaternion& fallback)
        {
            if (!jsonValue.is_array() || jsonValue.as_array().size() != 4)
            {
                return fallback;
            }

            const array& values = jsonValue.as_array();
            return Quaternion(static_cast<Float32>(values[0].as_double()),
                              static_cast<Float32>(values[1].as_double()),
                              static_cast<Float32>(values[2].as_double()),
                              static_cast<Float32>(values[3].as_double()));
        }

        [[nodiscard]] ResourceId ToResourceId(const value& jsonValue, ResourceId fallback) noexcept
        {
            if (jsonValue.is_uint64())
            {
                return jsonValue.as_uint64();
            }

            if (jsonValue.is_int64() && jsonValue.as_int64() >= 0)
            {
                return static_cast<ResourceId>(jsonValue.as_int64());
            }

            return fallback;
        }

        template<typename T>
        [[nodiscard]] ReflectedPropertyInfo
        MakeVector3Property(std::string name, Vector3 (T::*getter)() const, void (T::*setter)(const Vector3&))
        {
            ReflectedPropertyInfo property;
            property.name = std::move(name);
            property.type = ReflectedPropertyType::Vector3;
            property.serialize = [getter](const Component& component)
            { return ToJson((static_cast<const T&>(component).*getter)()); };
            property.deserialize = [getter, setter](Component& component, const value& jsonValue)
            {
                T& typed = static_cast<T&>(component);
                (typed.*setter)(ToVector3(jsonValue, (typed.*getter)()));
            };
            return property;
        }
    } // namespace

    void ReflectionRegistry::RegisterType(ReflectedTypeInfo typeInfo)
    {
        VE_ASSERT_MESSAGE(!typeInfo.name.empty(), "Reflected type name must not be empty.");
        typeLookup_[typeInfo.name] = types_.size();
        types_.push_back(std::move(typeInfo));
    }

    void ReflectionRegistry::RegisterEnum(ReflectedEnumInfo enumInfo)
    {
        VE_ASSERT_MESSAGE(!enumInfo.name.empty(), "Reflected enum name must not be empty.");
        enumLookup_[enumInfo.name] = enums_.size();
        enums_.push_back(std::move(enumInfo));
    }

    const ReflectedTypeInfo* ReflectionRegistry::FindType(std::string_view name) const noexcept
    {
        const auto iter = typeLookup_.find(std::string(name));
        return iter == typeLookup_.end() ? nullptr : &types_[iter->second];
    }

    const ReflectedEnumInfo* ReflectionRegistry::FindEnum(std::string_view name) const noexcept
    {
        const auto iter = enumLookup_.find(std::string(name));
        return iter == enumLookup_.end() ? nullptr : &enums_[iter->second];
    }

    std::unique_ptr<Component> ReflectionRegistry::CreateComponent(std::string_view typeName) const
    {
        const ReflectedTypeInfo* typeInfo = FindType(typeName);
        if (typeInfo == nullptr || !typeInfo->componentFactory)
        {
            return nullptr;
        }

        return typeInfo->componentFactory();
    }

    const std::vector<ReflectedTypeInfo>& ReflectionRegistry::GetTypes() const noexcept
    {
        return types_;
    }

    void RegisterSceneReflectionTypes(ReflectionRegistry& registry)
    {
        registry.RegisterEnum(
            ReflectedEnumInfo{"CameraProjectionMode",
                              {{"Perspective", static_cast<Int32>(CameraProjectionMode::Perspective)},
                               {"Orthographic", static_cast<Int32>(CameraProjectionMode::Orthographic)}}});
        registry.RegisterEnum(
            ReflectedEnumInfo{"LightType", {{"Directional", static_cast<Int32>(LightType::Directional)}}});

        ReflectedTypeInfo transform;
        transform.name = "TransformComponent";
        transform.baseTypeName = "Component";
        transform.componentFactory = []() { return std::make_unique<TransformComponent>(); };
        transform.properties.push_back(ReflectedPropertyInfo{
            "localPosition",
            ReflectedPropertyType::Vector3,
            true,
            true,
            {},
            [](const Component& component)
            { return ToJson(static_cast<const TransformComponent&>(component).GetLocalPosition()); },
            [](Component& component, const value& jsonValue)
            {
                auto& transform = static_cast<TransformComponent&>(component);
                transform.SetLocalPosition(ToVector3(jsonValue, transform.GetLocalPosition()));
            }});
        transform.properties.push_back(ReflectedPropertyInfo{
            "localRotation",
            ReflectedPropertyType::Quaternion,
            true,
            true,
            {},
            [](const Component& component)
            { return ToJson(static_cast<const TransformComponent&>(component).GetLocalRotation()); },
            [](Component& component, const value& jsonValue)
            {
                auto& transform = static_cast<TransformComponent&>(component);
                transform.SetLocalRotation(ToQuaternion(jsonValue, transform.GetLocalRotation()));
            }});
        transform.properties.push_back(
            ReflectedPropertyInfo{"localScale",
                                  ReflectedPropertyType::Vector3,
                                  true,
                                  true,
                                  {},
                                  [](const Component& component)
                                  { return ToJson(static_cast<const TransformComponent&>(component).GetLocalScale()); },
                                  [](Component& component, const value& jsonValue)
                                  {
                                      auto& transform = static_cast<TransformComponent&>(component);
                                      transform.SetLocalScale(ToVector3(jsonValue, transform.GetLocalScale()));
                                  }});
        registry.RegisterType(std::move(transform));

        ReflectedTypeInfo camera;
        camera.name = "CameraComponent";
        camera.baseTypeName = "Component";
        camera.componentFactory = []() { return std::make_unique<CameraComponent>(); };
        camera.properties.push_back(ReflectedPropertyInfo{
            "projectionMode",
            ReflectedPropertyType::Enum,
            true,
            true,
            "CameraProjectionMode",
            [](const Component& component)
            {
                const auto mode = static_cast<const CameraComponent&>(component).GetProjectionMode();
                return mode == CameraProjectionMode::Orthographic ? "Orthographic" : "Perspective";
            },
            [](Component& component, const value& jsonValue)
            {
                if (jsonValue.is_string())
                {
                    static_cast<CameraComponent&>(component).SetProjectionMode(jsonValue.as_string() == "Orthographic"
                                                                                   ? CameraProjectionMode::Orthographic
                                                                                   : CameraProjectionMode::Perspective);
                }
            }});
        camera.properties.push_back(
            ReflectedPropertyInfo{"fieldOfViewRadians",
                                  ReflectedPropertyType::Float32,
                                  true,
                                  true,
                                  {},
                                  [](const Component& component)
                                  { return static_cast<const CameraComponent&>(component).GetFieldOfViewRadians(); },
                                  [](Component& component, const value& jsonValue)
                                  {
                                      if (jsonValue.is_double())
                                      {
                                          static_cast<CameraComponent&>(component).SetFieldOfViewRadians(
                                              static_cast<Float32>(jsonValue.as_double()));
                                      }
                                  }});
        camera.properties.push_back(ReflectedPropertyInfo{
            "clearColor",
            ReflectedPropertyType::Vector4,
            true,
            true,
            {},
            [](const Component& component)
            { return ToJson(static_cast<const CameraComponent&>(component).GetClearColor()); },
            [](Component& component, const value& jsonValue)
            {
                auto& cameraComponent = static_cast<CameraComponent&>(component);
                cameraComponent.SetClearColor(ToVector4(jsonValue, cameraComponent.GetClearColor()));
            }});
        registry.RegisterType(std::move(camera));

        ReflectedTypeInfo meshRenderer;
        meshRenderer.name = "MeshRendererComponent";
        meshRenderer.baseTypeName = "Component";
        meshRenderer.componentFactory = []() { return std::make_unique<MeshRendererComponent>(); };
        meshRenderer.properties.push_back(
            ReflectedPropertyInfo{"mesh",
                                  ReflectedPropertyType::ResourceId,
                                  true,
                                  true,
                                  {},
                                  [](const Component& component)
                                  {
                                      return static_cast<std::uint64_t>(
                                          static_cast<const MeshRendererComponent&>(component).GetMesh().GetId());
                                  },
                                  [](Component& component, const value& jsonValue)
                                  {
                                      auto& renderer = static_cast<MeshRendererComponent&>(component);
                                      renderer.SetMesh(ResourceHandle<MeshResource>(
                                          ToResourceId(jsonValue, renderer.GetMesh().GetId())));
                                  }});
        meshRenderer.properties.push_back(
            ReflectedPropertyInfo{"material",
                                  ReflectedPropertyType::ResourceId,
                                  true,
                                  true,
                                  {},
                                  [](const Component& component)
                                  {
                                      return static_cast<std::uint64_t>(
                                          static_cast<const MeshRendererComponent&>(component).GetMaterial().GetId());
                                  },
                                  [](Component& component, const value& jsonValue)
                                  {
                                      auto& renderer = static_cast<MeshRendererComponent&>(component);
                                      renderer.SetMaterial(ResourceHandle<MaterialResource>(
                                          ToResourceId(jsonValue, renderer.GetMaterial().GetId())));
                                  }});
        meshRenderer.properties.push_back(ReflectedPropertyInfo{
            "visible",
            ReflectedPropertyType::Bool,
            true,
            true,
            {},
            [](const Component& component) { return static_cast<const MeshRendererComponent&>(component).IsVisible(); },
            [](Component& component, const value& jsonValue)
            {
                if (jsonValue.is_bool())
                {
                    static_cast<MeshRendererComponent&>(component).SetVisible(jsonValue.as_bool());
                }
            }});
        registry.RegisterType(std::move(meshRenderer));

        ReflectedTypeInfo light;
        light.name = "LightComponent";
        light.baseTypeName = "Component";
        light.componentFactory = []() { return std::make_unique<LightComponent>(); };
        light.properties.push_back(ReflectedPropertyInfo{
            "color",
            ReflectedPropertyType::Vector3,
            true,
            true,
            {},
            [](const Component& component) { return ToJson(static_cast<const LightComponent&>(component).GetColor()); },
            [](Component& component, const value& jsonValue)
            {
                auto& lightComponent = static_cast<LightComponent&>(component);
                lightComponent.SetColor(ToVector3(jsonValue, lightComponent.GetColor()));
            }});
        light.properties.push_back(ReflectedPropertyInfo{
            "intensity",
            ReflectedPropertyType::Float32,
            true,
            true,
            {},
            [](const Component& component) { return static_cast<const LightComponent&>(component).GetIntensity(); },
            [](Component& component, const value& jsonValue)
            {
                if (jsonValue.is_double())
                {
                    static_cast<LightComponent&>(component).SetIntensity(static_cast<Float32>(jsonValue.as_double()));
                }
            }});
        registry.RegisterType(std::move(light));
    }
} // namespace ve
