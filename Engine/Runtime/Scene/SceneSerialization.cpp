#include "Engine/Runtime/Scene/SceneSerialization.h"

#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <boost/json.hpp>

#include <string>
#include <string_view>

namespace ve
{
    namespace
    {
        [[nodiscard]] Error MakeSceneJsonError(std::string message)
        {
            return Error(ErrorCode::InvalidArgument, std::move(message));
        }

        [[nodiscard]] Float32 ReadNumberAsFloat(const boost::json::value& value) noexcept
        {
            if (value.is_double())
            {
                return static_cast<Float32>(value.as_double());
            }

            if (value.is_int64())
            {
                return static_cast<Float32>(value.as_int64());
            }

            if (value.is_uint64())
            {
                return static_cast<Float32>(value.as_uint64());
            }

            return 0.0f;
        }

        [[nodiscard]] boost::json::array WriteVector3(const Vector3& value)
        {
            boost::json::array array;
            array.reserve(3);
            array.push_back(value.GetX());
            array.push_back(value.GetY());
            array.push_back(value.GetZ());
            return array;
        }

        [[nodiscard]] boost::json::array WriteQuaternion(const Quaternion& value)
        {
            boost::json::array array;
            array.reserve(4);
            array.push_back(value.GetX());
            array.push_back(value.GetY());
            array.push_back(value.GetZ());
            array.push_back(value.GetW());
            return array;
        }

        [[nodiscard]] Result<Vector3> ReadVector3(const boost::json::value& value, std::string_view fieldName)
        {
            if (!value.is_array() || value.as_array().size() != 3)
            {
                return Result<Vector3>::Failure(MakeSceneJsonError(std::string(fieldName) + " must be a 3-item array."));
            }

            const boost::json::array& array = value.as_array();
            if (!array[0].is_number() || !array[1].is_number() || !array[2].is_number())
            {
                return Result<Vector3>::Failure(
                    MakeSceneJsonError(std::string(fieldName) + " must contain numeric values."));
            }

            return Result<Vector3>::Success(
                Vector3(ReadNumberAsFloat(array[0]), ReadNumberAsFloat(array[1]), ReadNumberAsFloat(array[2])));
        }

        [[nodiscard]] Result<Quaternion> ReadQuaternion(const boost::json::value& value, std::string_view fieldName)
        {
            if (!value.is_array() || value.as_array().size() != 4)
            {
                return Result<Quaternion>::Failure(
                    MakeSceneJsonError(std::string(fieldName) + " must be a 4-item array."));
            }

            const boost::json::array& array = value.as_array();
            if (!array[0].is_number() || !array[1].is_number() || !array[2].is_number() || !array[3].is_number())
            {
                return Result<Quaternion>::Failure(
                    MakeSceneJsonError(std::string(fieldName) + " must contain numeric values."));
            }

            return Result<Quaternion>::Success(Quaternion(ReadNumberAsFloat(array[0]),
                                                          ReadNumberAsFloat(array[1]),
                                                          ReadNumberAsFloat(array[2]),
                                                          ReadNumberAsFloat(array[3])));
        }

        [[nodiscard]] std::string ReadString(const boost::json::object& object,
                                             boost::json::string_view key,
                                             std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] Guid ReadGuid(const boost::json::object& object, boost::json::string_view key, const Guid& fallback)
        {
            const std::string text = ReadString(object, key);
            if (text.empty())
            {
                return fallback;
            }

            Result<Guid> guid = Guid::Parse(text);
            if (!guid)
            {
                return fallback;
            }

            return guid.GetValue();
        }

        [[nodiscard]] bool ReadBool(const boost::json::object& object, boost::json::string_view key, bool fallback)
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_bool())
            {
                return value->as_bool();
            }

            return fallback;
        }

        [[nodiscard]] Float32 ReadFloat(const boost::json::object& object, boost::json::string_view key, Float32 fallback)
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_number())
            {
                return ReadNumberAsFloat(*value);
            }

            return fallback;
        }

        [[nodiscard]] const boost::json::object* FindComponent(const boost::json::array& components,
                                                               std::string_view type)
        {
            for (const boost::json::value& componentValue : components)
            {
                if (!componentValue.is_object())
                {
                    continue;
                }

                const boost::json::object& componentObject = componentValue.as_object();
                const boost::json::value* typeValue = componentObject.if_contains("type");
                if (typeValue != nullptr && typeValue->is_string() && typeValue->as_string() == type)
                {
                    return &componentObject;
                }
            }

            return nullptr;
        }

        [[nodiscard]] const char* ToString(CameraComponent::ProjectionMode mode) noexcept
        {
            switch (mode)
            {
            case CameraComponent::ProjectionMode::Perspective:
                return "Perspective";
            case CameraComponent::ProjectionMode::Orthographic:
                return "Orthographic";
            }

            return "Perspective";
        }

        [[nodiscard]] CameraComponent::ProjectionMode ParseProjectionMode(std::string_view text) noexcept
        {
            if (text == "Orthographic")
            {
                return CameraComponent::ProjectionMode::Orthographic;
            }

            return CameraComponent::ProjectionMode::Perspective;
        }

        [[nodiscard]] const char* ToString(LightType type) noexcept
        {
            switch (type)
            {
            case LightType::Directional:
                return "Directional";
            case LightType::Point:
                return "Point";
            }

            return "Directional";
        }

        [[nodiscard]] LightType ParseLightType(std::string_view text) noexcept
        {
            if (text == "Point")
            {
                return LightType::Point;
            }

            return LightType::Directional;
        }

        [[nodiscard]] boost::json::object WriteTransformComponent(const TransformComponent& transform)
        {
            boost::json::object object;
            object["type"] = "TransformComponent";
            object["enabled"] = transform.IsEnabled();
            object["localPosition"] = WriteVector3(transform.GetLocalPosition());
            object["localRotation"] = WriteQuaternion(transform.GetLocalRotation());
            object["localScale"] = WriteVector3(transform.GetLocalScale());
            return object;
        }

        [[nodiscard]] boost::json::object WriteMeshRenderComponent(const MeshRenderComponent& mesh)
        {
            boost::json::object object;
            object["type"] = "MeshRenderComponent";
            object["enabled"] = mesh.IsEnabled();
            object["meshAssetGuid"] = mesh.GetMeshAssetGuid().ToString();
            object["materialAssetGuid"] = mesh.GetMaterialAssetGuid().ToString();
            object["boundsCenter"] = WriteVector3(mesh.GetBoundsCenter());
            object["boundsExtents"] = WriteVector3(mesh.GetBoundsExtents());
            return object;
        }

        [[nodiscard]] boost::json::object WriteCameraComponent(const CameraComponent& camera)
        {
            boost::json::object object;
            object["type"] = "CameraComponent";
            object["enabled"] = camera.IsEnabled();
            object["primary"] = camera.IsPrimary();
            object["projectionMode"] = ToString(camera.GetProjectionMode());
            object["verticalFieldOfViewRadians"] = camera.GetVerticalFieldOfViewRadians();
            object["orthographicSize"] = camera.GetOrthographicSize();
            object["aspectRatio"] = camera.GetAspectRatio();
            object["nearClipPlane"] = camera.GetNearClipPlane();
            object["farClipPlane"] = camera.GetFarClipPlane();
            return object;
        }

        [[nodiscard]] boost::json::object WriteLightComponent(const LightComponent& light)
        {
            boost::json::object object;
            object["type"] = "LightComponent";
            object["enabled"] = light.IsEnabled();
            object["lightType"] = ToString(light.GetLightType());
            object["color"] = WriteVector3(light.GetColor());
            object["intensity"] = light.GetIntensity();
            object["range"] = light.GetRange();
            object["innerConeAngleRadians"] = light.GetInnerConeAngleRadians();
            object["outerConeAngleRadians"] = light.GetOuterConeAngleRadians();
            object["castShadows"] = light.CastShadows();
            return object;
        }

        [[nodiscard]] boost::json::object WriteGameObject(const GameObject& gameObject)
        {
            boost::json::object object;
            object["name"] = gameObject.GetName();

            boost::json::array components;
            if (const TransformComponent* transform = gameObject.GetComponent<TransformComponent>(); transform != nullptr)
            {
                components.push_back(WriteTransformComponent(*transform));
            }

            if (const MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>(); mesh != nullptr)
            {
                components.push_back(WriteMeshRenderComponent(*mesh));
            }

            if (const CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr)
            {
                components.push_back(WriteCameraComponent(*camera));
            }

            if (const LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr)
            {
                components.push_back(WriteLightComponent(*light));
            }
            object["components"] = std::move(components);

            boost::json::array children;
            if (const TransformComponent* transform = gameObject.GetComponent<TransformComponent>(); transform != nullptr)
            {
                for (SizeT index = 0; index < transform->GetChildCount(); ++index)
                {
                    const GameObject* child = transform->GetChildGameObject(index);
                    if (child != nullptr)
                    {
                        children.push_back(WriteGameObject(*child));
                    }
                }
            }
            object["children"] = std::move(children);
            return object;
        }

        [[nodiscard]] Result<boost::json::object> WriteScene(const Scene& scene)
        {
            boost::json::object object;
            object["schemaVersion"] = 1;
            object["name"] = scene.GetName();

            boost::json::array roots;
            for (SizeT index = 0; index < scene.GetRootGameObjectCount(); ++index)
            {
                const GameObject* gameObject = scene.GetRootGameObject(index);
                if (gameObject != nullptr)
                {
                    roots.push_back(WriteGameObject(*gameObject));
                }
            }
            object["rootGameObjects"] = std::move(roots);
            return Result<boost::json::object>::Success(std::move(object));
        }

        [[nodiscard]] ErrorCode ApplyTransformComponent(GameObject& gameObject, const boost::json::object& object)
        {
            TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            if (transform == nullptr)
            {
                return ErrorCode::InvalidState;
            }

            if (const boost::json::value* value = object.if_contains("localPosition"); value != nullptr)
            {
                Result<Vector3> result = ReadVector3(*value, "TransformComponent.localPosition");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                transform->SetLocalPosition(result.GetValue());
            }

            if (const boost::json::value* value = object.if_contains("localRotation"); value != nullptr)
            {
                Result<Quaternion> result = ReadQuaternion(*value, "TransformComponent.localRotation");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                transform->SetLocalRotation(result.GetValue());
            }

            if (const boost::json::value* value = object.if_contains("localScale"); value != nullptr)
            {
                Result<Vector3> result = ReadVector3(*value, "TransformComponent.localScale");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                transform->SetLocalScale(result.GetValue());
            }

            transform->SetEnabled(ReadBool(object, "enabled", transform->IsEnabled()));
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ApplyMeshRenderComponent(GameObject& gameObject, const boost::json::object& object)
        {
            MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>();
            if (mesh == nullptr)
            {
                Result<MeshRenderComponent*> result = gameObject.AddComponentWithoutRenderRegistration<MeshRenderComponent>();
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                mesh = result.GetValue();
            }

            mesh->SetMeshAssetGuid(ReadGuid(object, "meshAssetGuid", mesh->GetMeshAssetGuid()));
            mesh->SetMaterialAssetGuid(ReadGuid(object, "materialAssetGuid", mesh->GetMaterialAssetGuid()));

            if (const boost::json::value* value = object.if_contains("boundsCenter"); value != nullptr)
            {
                Result<Vector3> result = ReadVector3(*value, "MeshRenderComponent.boundsCenter");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                mesh->SetBoundsCenter(result.GetValue());
            }

            if (const boost::json::value* value = object.if_contains("boundsExtents"); value != nullptr)
            {
                Result<Vector3> result = ReadVector3(*value, "MeshRenderComponent.boundsExtents");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                mesh->SetBoundsExtents(result.GetValue());
            }

            mesh->SetEnabled(ReadBool(object, "enabled", mesh->IsEnabled()));
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ApplyCameraComponent(GameObject& gameObject, const boost::json::object& object)
        {
            CameraComponent* camera = gameObject.GetComponent<CameraComponent>();
            if (camera == nullptr)
            {
                Result<CameraComponent*> result = gameObject.AddComponentWithoutRenderRegistration<CameraComponent>();
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                camera = result.GetValue();
            }

            camera->SetPrimary(ReadBool(object, "primary", camera->IsPrimary()));
            camera->SetProjectionMode(ParseProjectionMode(ReadString(object, "projectionMode", ToString(camera->GetProjectionMode()))));
            camera->SetVerticalFieldOfViewRadians(
                ReadFloat(object, "verticalFieldOfViewRadians", camera->GetVerticalFieldOfViewRadians()));
            camera->SetOrthographicSize(ReadFloat(object, "orthographicSize", camera->GetOrthographicSize()));
            camera->SetAspectRatio(ReadFloat(object, "aspectRatio", camera->GetAspectRatio()));
            camera->SetNearClipPlane(ReadFloat(object, "nearClipPlane", camera->GetNearClipPlane()));
            camera->SetFarClipPlane(ReadFloat(object, "farClipPlane", camera->GetFarClipPlane()));
            camera->SetEnabled(ReadBool(object, "enabled", camera->IsEnabled()));
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ApplyLightComponent(GameObject& gameObject, const boost::json::object& object)
        {
            LightComponent* light = gameObject.GetComponent<LightComponent>();
            if (light == nullptr)
            {
                Result<LightComponent*> result = gameObject.AddComponentWithoutRenderRegistration<LightComponent>();
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                light = result.GetValue();
            }

            light->SetLightType(ParseLightType(ReadString(object, "lightType", ToString(light->GetLightType()))));

            if (const boost::json::value* value = object.if_contains("color"); value != nullptr)
            {
                Result<Vector3> result = ReadVector3(*value, "LightComponent.color");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                light->SetColor(result.GetValue());
            }

            light->SetIntensity(ReadFloat(object, "intensity", light->GetIntensity()));
            light->SetRange(ReadFloat(object, "range", light->GetRange()));
            light->SetInnerConeAngleRadians(ReadFloat(object, "innerConeAngleRadians", light->GetInnerConeAngleRadians()));
            light->SetOuterConeAngleRadians(ReadFloat(object, "outerConeAngleRadians", light->GetOuterConeAngleRadians()));
            light->SetCastShadows(ReadBool(object, "castShadows", light->CastShadows()));
            light->SetEnabled(ReadBool(object, "enabled", light->IsEnabled()));
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ApplyComponents(GameObject& gameObject, const boost::json::array& components)
        {
            if (const boost::json::object* transform = FindComponent(components, "TransformComponent"); transform != nullptr)
            {
                const ErrorCode result = ApplyTransformComponent(gameObject, *transform);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            if (const boost::json::object* mesh = FindComponent(components, "MeshRenderComponent"); mesh != nullptr)
            {
                const ErrorCode result = ApplyMeshRenderComponent(gameObject, *mesh);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            if (const boost::json::object* camera = FindComponent(components, "CameraComponent"); camera != nullptr)
            {
                const ErrorCode result = ApplyCameraComponent(gameObject, *camera);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            if (const boost::json::object* light = FindComponent(components, "LightComponent"); light != nullptr)
            {
                const ErrorCode result = ApplyLightComponent(gameObject, *light);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ReadGameObjectRecursive(Scene& scene,
                                                        TransformComponent* parent,
                                                        const boost::json::object& object)
        {
            const std::string name = ReadString(object, "name");

            GameObject* gameObject = nullptr;
            if (parent == nullptr)
            {
                Result<GameObject*> result = scene.CreateRootGameObjectWithoutRenderRegistration(name);
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                gameObject = result.GetValue();
            }
            else
            {
                Result<GameObject*> result = parent->CreateChild(name);
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                gameObject = result.GetValue();
            }

            if (gameObject == nullptr)
            {
                return ErrorCode::InvalidState;
            }

            if (const boost::json::value* componentsValue = object.if_contains("components"); componentsValue != nullptr)
            {
                if (!componentsValue->is_array())
                {
                    return ErrorCode::InvalidArgument;
                }

                const ErrorCode result = ApplyComponents(*gameObject, componentsValue->as_array());
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            if (const boost::json::value* childrenValue = object.if_contains("children"); childrenValue != nullptr)
            {
                if (!childrenValue->is_array())
                {
                    return ErrorCode::InvalidArgument;
                }

                TransformComponent* transform = gameObject->GetComponent<TransformComponent>();
                if (transform == nullptr)
                {
                    return ErrorCode::InvalidState;
                }

                for (const boost::json::value& childValue : childrenValue->as_array())
                {
                    if (!childValue.is_object())
                    {
                        return ErrorCode::InvalidArgument;
                    }

                    const ErrorCode result = ReadGameObjectRecursive(scene, transform, childValue.as_object());
                    if (result != ErrorCode::None)
                    {
                        return result;
                    }
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ReadScene(Scene& scene, const boost::json::object& object)
        {
            scene.Clear();
            scene.SetName(ReadString(object, "name"));

            const boost::json::value* rootsValue = object.if_contains("rootGameObjects");
            if (rootsValue == nullptr)
            {
                return ErrorCode::None;
            }

            if (!rootsValue->is_array())
            {
                return ErrorCode::InvalidArgument;
            }

            for (const boost::json::value& rootValue : rootsValue->as_array())
            {
                if (!rootValue.is_object())
                {
                    return ErrorCode::InvalidArgument;
                }

                const ErrorCode result = ReadGameObjectRecursive(scene, nullptr, rootValue.as_object());
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            scene.RebuildRenderThreadScene();
            return ErrorCode::None;
        }
    } // namespace

    ErrorCode SceneSerialization::SaveToFile(const Scene& scene, const Path& path)
    {
        Result<std::string> textResult = SaveToString(scene);
        if (!textResult)
        {
            return textResult.GetError().GetCode();
        }

        return FileSystem::WriteTextFile(FileSystem::ResolveProjectPath(path), textResult.GetValue());
    }

    ErrorCode SceneSerialization::LoadFromFile(Scene& scene, const Path& path)
    {
        Result<std::string> textResult = FileSystem::ReadTextFile(FileSystem::ResolveProjectPath(path));
        if (!textResult)
        {
            return textResult.GetError().GetCode();
        }

        return LoadFromString(scene, textResult.GetValue());
    }

    Result<std::string> SceneSerialization::SaveToString(const Scene& scene)
    {
        Result<boost::json::object> objectResult = WriteScene(scene);
        if (!objectResult)
        {
            return Result<std::string>::Failure(objectResult.GetError());
        }

        return Result<std::string>::Success(JsonUtils::SerializePretty(boost::json::value(objectResult.MoveValue())));
    }

    ErrorCode SceneSerialization::LoadFromString(Scene& scene, std::string_view text)
    {
        Result<boost::json::value> jsonResult = JsonUtils::Parse(text);
        if (!jsonResult)
        {
            return jsonResult.GetError().GetCode();
        }

        const boost::json::value& value = jsonResult.GetValue();
        if (!value.is_object())
        {
            return ErrorCode::InvalidArgument;
        }

        return ReadScene(scene, value.as_object());
    }
} // namespace ve
