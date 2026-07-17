#include "Engine/Runtime/Scene/SceneSerialization.h"

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/ColliderComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/RigidbodyComponent.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"

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

        [[nodiscard]] boost::json::array WriteColor(const rhi::RhiColor& value)
        {
            boost::json::array array;
            array.reserve(4);
            array.push_back(value.r);
            array.push_back(value.g);
            array.push_back(value.b);
            array.push_back(value.a);
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
                return Result<Vector3>::Failure(MakeSceneJsonError(std::string(fieldName) + " must contain numeric values."));
            }

            return Result<Vector3>::Success(Vector3(ReadNumberAsFloat(array[0]), ReadNumberAsFloat(array[1]), ReadNumberAsFloat(array[2])));
        }

        [[nodiscard]] Result<Quaternion> ReadQuaternion(const boost::json::value& value, std::string_view fieldName)
        {
            if (!value.is_array() || value.as_array().size() != 4)
            {
                return Result<Quaternion>::Failure(MakeSceneJsonError(std::string(fieldName) + " must be a 4-item array."));
            }

            const boost::json::array& array = value.as_array();
            if (!array[0].is_number() || !array[1].is_number() || !array[2].is_number() || !array[3].is_number())
            {
                return Result<Quaternion>::Failure(MakeSceneJsonError(std::string(fieldName) + " must contain numeric values."));
            }

            return Result<Quaternion>::Success(
                Quaternion(ReadNumberAsFloat(array[0]), ReadNumberAsFloat(array[1]), ReadNumberAsFloat(array[2]), ReadNumberAsFloat(array[3])));
        }

        [[nodiscard]] Result<rhi::RhiColor> ReadColor(const boost::json::value& value, std::string_view fieldName)
        {
            if (!value.is_array() || value.as_array().size() != 4)
            {
                return Result<rhi::RhiColor>::Failure(MakeSceneJsonError(std::string(fieldName) + " must be a 4-item array."));
            }

            const boost::json::array& array = value.as_array();
            if (!array[0].is_number() || !array[1].is_number() || !array[2].is_number() || !array[3].is_number())
            {
                return Result<rhi::RhiColor>::Failure(MakeSceneJsonError(std::string(fieldName) + " must contain numeric values."));
            }

            return Result<rhi::RhiColor>::Success(
                rhi::RhiColor{ReadNumberAsFloat(array[0]), ReadNumberAsFloat(array[1]), ReadNumberAsFloat(array[2]), ReadNumberAsFloat(array[3])});
        }

        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
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

        [[nodiscard]] UInt64 ReadUInt64(const boost::json::object& object, boost::json::string_view key, UInt64 fallback = 0)
        {
            const boost::json::value* value = object.if_contains(key);
            if (value == nullptr)
            {
                return fallback;
            }

            if (value->is_uint64())
            {
                return static_cast<UInt64>(value->as_uint64());
            }

            if (value->is_int64() && value->as_int64() >= 0)
            {
                return static_cast<UInt64>(value->as_int64());
            }

            return fallback;
        }

        [[nodiscard]] boost::json::object WriteAssetID(const AssetID& id)
        {
            boost::json::object object;
            object["guid"] = id.GetGuid().ToString();
            object["subID"] = id.GetSubID();
            return object;
        }

        [[nodiscard]] boost::json::object WriteAssetRef(const AssetRefBase& assetRef)
        {
            boost::json::object object;
            object["assetID"] = WriteAssetID(assetRef.GetAssetID());
            return object;
        }

        [[nodiscard]] AssetID ReadAssetID(const boost::json::object& object, const AssetID& fallback)
        {
            const std::string guidText = ReadString(object, "guid");
            if (guidText.empty())
            {
                return fallback;
            }

            Result<Guid> guid = Guid::Parse(guidText);
            if (!guid)
            {
                return fallback;
            }

            return AssetID(guid.GetValue(), ReadUInt64(object, "subID", 0));
        }

        [[nodiscard]] AssetID
        ReadAssetRef(const boost::json::object& object, boost::json::string_view objectKey, boost::json::string_view legacyGuidKey, const AssetID& fallback)
        {
            if (const boost::json::value* value = object.if_contains(objectKey); value != nullptr && value->is_object())
            {
                const boost::json::object& assetRefObject = value->as_object();
                if (const boost::json::value* assetIDValue = assetRefObject.if_contains("assetID"); assetIDValue != nullptr && assetIDValue->is_object())
                {
                    return ReadAssetID(assetIDValue->as_object(), fallback);
                }
            }

            const Guid legacyGuid = ReadGuid(object, legacyGuidKey, fallback.GetGuid());
            return legacyGuid.IsEmpty() ? fallback : AssetID(legacyGuid, 0);
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

        [[nodiscard]] const boost::json::object* FindComponent(const boost::json::array& components, std::string_view type)
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

        [[nodiscard]] const char* ToString(ColliderShapeType type) noexcept
        {
            switch (type)
            {
            case ColliderShapeType::Box:
                return "Box";
            case ColliderShapeType::Sphere:
                return "Sphere";
            case ColliderShapeType::Capsule:
                return "Capsule";
            }

            return "Box";
        }

        [[nodiscard]] ColliderShapeType ParseColliderShapeType(std::string_view text) noexcept
        {
            if (text == "Sphere")
            {
                return ColliderShapeType::Sphere;
            }

            if (text == "Capsule")
            {
                return ColliderShapeType::Capsule;
            }

            return ColliderShapeType::Box;
        }

        [[nodiscard]] const char* ToString(ColliderDirectionAxis direction) noexcept
        {
            switch (direction)
            {
            case ColliderDirectionAxis::X:
                return "X";
            case ColliderDirectionAxis::Y:
                return "Y";
            case ColliderDirectionAxis::Z:
                return "Z";
            }

            return "Y";
        }

        [[nodiscard]] ColliderDirectionAxis ParseColliderDirectionAxis(std::string_view text) noexcept
        {
            if (text == "X")
            {
                return ColliderDirectionAxis::X;
            }

            if (text == "Z")
            {
                return ColliderDirectionAxis::Z;
            }

            return ColliderDirectionAxis::Y;
        }

        [[nodiscard]] const char* ToString(RigidbodyInterpolationMode mode) noexcept
        {
            switch (mode)
            {
            case RigidbodyInterpolationMode::None:
                return "None";
            case RigidbodyInterpolationMode::Interpolate:
                return "Interpolate";
            case RigidbodyInterpolationMode::Extrapolate:
                return "Extrapolate";
            }

            return "None";
        }

        [[nodiscard]] RigidbodyInterpolationMode ParseRigidbodyInterpolationMode(std::string_view text) noexcept
        {
            if (text == "Interpolate")
            {
                return RigidbodyInterpolationMode::Interpolate;
            }

            if (text == "Extrapolate")
            {
                return RigidbodyInterpolationMode::Extrapolate;
            }

            return RigidbodyInterpolationMode::None;
        }

        [[nodiscard]] const char* ToString(RigidbodyCollisionDetectionMode mode) noexcept
        {
            switch (mode)
            {
            case RigidbodyCollisionDetectionMode::Discrete:
                return "Discrete";
            case RigidbodyCollisionDetectionMode::Continuous:
                return "Continuous";
            case RigidbodyCollisionDetectionMode::ContinuousDynamic:
                return "ContinuousDynamic";
            case RigidbodyCollisionDetectionMode::ContinuousSpeculative:
                return "ContinuousSpeculative";
            }

            return "Discrete";
        }

        [[nodiscard]] RigidbodyCollisionDetectionMode ParseRigidbodyCollisionDetectionMode(std::string_view text) noexcept
        {
            if (text == "Continuous")
            {
                return RigidbodyCollisionDetectionMode::Continuous;
            }

            if (text == "ContinuousDynamic")
            {
                return RigidbodyCollisionDetectionMode::ContinuousDynamic;
            }

            if (text == "ContinuousSpeculative")
            {
                return RigidbodyCollisionDetectionMode::ContinuousSpeculative;
            }

            return RigidbodyCollisionDetectionMode::Discrete;
        }

        [[nodiscard]] bool HasConstraint(RigidbodyConstraintFlags flags, RigidbodyConstraintFlags flag) noexcept
        {
            return (ToUnderlying(flags) & ToUnderlying(flag)) != 0;
        }

        void PushConstraintName(boost::json::array& array, RigidbodyConstraintFlags flags, RigidbodyConstraintFlags flag, const char* name)
        {
            if (HasConstraint(flags, flag))
            {
                array.push_back(name);
            }
        }

        [[nodiscard]] boost::json::array WriteRigidbodyConstraints(RigidbodyConstraintFlags constraints)
        {
            boost::json::array array;
            PushConstraintName(array, constraints, RigidbodyConstraintFlags::FreezePositionX, "FreezePositionX");
            PushConstraintName(array, constraints, RigidbodyConstraintFlags::FreezePositionY, "FreezePositionY");
            PushConstraintName(array, constraints, RigidbodyConstraintFlags::FreezePositionZ, "FreezePositionZ");
            PushConstraintName(array, constraints, RigidbodyConstraintFlags::FreezeRotationX, "FreezeRotationX");
            PushConstraintName(array, constraints, RigidbodyConstraintFlags::FreezeRotationY, "FreezeRotationY");
            PushConstraintName(array, constraints, RigidbodyConstraintFlags::FreezeRotationZ, "FreezeRotationZ");
            return array;
        }

        [[nodiscard]] RigidbodyConstraintFlags ParseRigidbodyConstraintName(std::string_view text) noexcept
        {
            if (text == "FreezePositionX")
            {
                return RigidbodyConstraintFlags::FreezePositionX;
            }

            if (text == "FreezePositionY")
            {
                return RigidbodyConstraintFlags::FreezePositionY;
            }

            if (text == "FreezePositionZ")
            {
                return RigidbodyConstraintFlags::FreezePositionZ;
            }

            if (text == "FreezeRotationX")
            {
                return RigidbodyConstraintFlags::FreezeRotationX;
            }

            if (text == "FreezeRotationY")
            {
                return RigidbodyConstraintFlags::FreezeRotationY;
            }

            if (text == "FreezeRotationZ")
            {
                return RigidbodyConstraintFlags::FreezeRotationZ;
            }

            if (text == "FreezePosition")
            {
                return RigidbodyConstraintFlags::FreezePosition;
            }

            if (text == "FreezeRotation")
            {
                return RigidbodyConstraintFlags::FreezeRotation;
            }

            if (text == "FreezeAll")
            {
                return RigidbodyConstraintFlags::FreezeAll;
            }

            return RigidbodyConstraintFlags::None;
        }

        [[nodiscard]] RigidbodyConstraintFlags
        ReadRigidbodyConstraints(const boost::json::object& object, boost::json::string_view key, RigidbodyConstraintFlags fallback)
        {
            const boost::json::value* value = object.if_contains(key);
            if (value == nullptr)
            {
                return fallback;
            }

            if (value->is_string())
            {
                const boost::json::string& text = value->as_string();
                return ParseRigidbodyConstraintName(std::string_view(text.data(), text.size()));
            }

            if (!value->is_array())
            {
                return fallback;
            }

            RigidbodyConstraintFlags constraints = RigidbodyConstraintFlags::None;
            for (const boost::json::value& constraintValue : value->as_array())
            {
                if (!constraintValue.is_string())
                {
                    continue;
                }

                const boost::json::string& text = constraintValue.as_string();
                constraints |= ParseRigidbodyConstraintName(std::string_view(text.data(), text.size()));
            }

            return constraints;
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
            object["mesh"] = WriteAssetRef(mesh.GetMesh());
            object["material"] = WriteAssetRef(mesh.GetMaterial());
            object["boundsCenter"] = WriteVector3(mesh.GetBoundsCenter());
            object["boundsExtents"] = WriteVector3(mesh.GetBoundsExtents());
            return object;
        }

        [[nodiscard]] boost::json::object WriteCameraComponent(const CameraComponent& camera)
        {
            boost::json::object object;
            object["type"] = "CameraComponent";
            object["enabled"] = camera.IsEnabled();
            object["projectionMode"] = ToString(camera.GetProjectionMode());
            object["verticalFieldOfViewRadians"] = camera.GetVerticalFieldOfViewRadians();
            object["orthographicSize"] = camera.GetOrthographicSize();
            object["automaticAspectRatio"] = camera.IsAspectRatioAutomatic();
            object["aspectRatio"] = camera.GetAspectRatio();
            object["nearClipPlane"] = camera.GetNearClipPlane();
            object["farClipPlane"] = camera.GetFarClipPlane();
            object["clearColor"] = WriteColor(camera.GetClearColor());
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

        [[nodiscard]] boost::json::object WriteColliderComponent(const ColliderComponent& collider)
        {
            const ColliderDesc& desc = collider.GetDesc();

            boost::json::object object;
            object["type"] = "ColliderComponent";
            object["enabled"] = collider.IsEnabled();
            object["shapeType"] = ToString(desc.shapeType);
            object["isTrigger"] = desc.trigger;
            object["center"] = WriteVector3(desc.center);
            object["size"] = WriteVector3(desc.size);
            object["radius"] = desc.radius;
            object["height"] = desc.height;
            object["direction"] = ToString(desc.direction);
            object["staticFriction"] = desc.staticFriction;
            object["dynamicFriction"] = desc.dynamicFriction;
            object["bounciness"] = desc.bounciness;
            return object;
        }

        [[nodiscard]] boost::json::object WriteRigidbodyComponent(const RigidbodyComponent& rigidbody)
        {
            const RigidbodyDesc& desc = rigidbody.GetDesc();

            boost::json::object object;
            object["type"] = "RigidbodyComponent";
            object["enabled"] = rigidbody.IsEnabled();
            object["mass"] = desc.mass;
            object["linearDamping"] = desc.linearDamping;
            object["angularDamping"] = desc.angularDamping;
            object["useGravity"] = desc.useGravity;
            object["isKinematic"] = desc.kinematic;
            object["detectCollisions"] = desc.detectCollisions;
            object["interpolation"] = ToString(desc.interpolationMode);
            object["collisionDetection"] = ToString(desc.collisionDetectionMode);
            object["constraints"] = WriteRigidbodyConstraints(desc.constraints);
            return object;
        }

        [[nodiscard]] boost::json::object WriteDotnetScriptableComponent(const DotnetScriptableComponent& script)
        {
            boost::json::object object;
            object["type"] = "DotnetScriptableComponent";
            object["enabled"] = script.IsEnabled();
            object["scriptTypeName"] = script.GetScriptTypeName();
            Result<boost::json::object> fields = script.GetScriptFields();
            if (fields)
            {
                object["serializedFields"] = fields.MoveValue();
            }
            else
            {
                VE_LOG_WARN_CATEGORY("Scene", "Failed to serialize script fields '{}': {}", script.GetScriptTypeName(), fields.GetError().GetMessage());
                object["serializedFields"] = boost::json::object();
            }
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

            if (const ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>(); collider != nullptr)
            {
                components.push_back(WriteColliderComponent(*collider));
            }

            if (const RigidbodyComponent* rigidbody = gameObject.GetComponent<RigidbodyComponent>(); rigidbody != nullptr)
            {
                components.push_back(WriteRigidbodyComponent(*rigidbody));
            }

            for (SizeT scriptIndex = 0; scriptIndex < gameObject.GetScriptableComponentCount(); ++scriptIndex)
            {
                const ScriptableComponent* script = gameObject.GetScriptableComponent(scriptIndex);
                const DotnetScriptableComponent* dotnetScript = dynamic_cast<const DotnetScriptableComponent*>(script);
                if (dotnetScript != nullptr)
                {
                    components.push_back(WriteDotnetScriptableComponent(*dotnetScript));
                }
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

            mesh->SetMeshAssetID(ReadAssetRef(object, "mesh", "meshAssetGuid", mesh->GetMeshAssetID()));
            mesh->SetMaterialAssetID(ReadAssetRef(object, "material", "materialAssetGuid", mesh->GetMaterialAssetID()));

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

            camera->SetProjectionMode(ParseProjectionMode(ReadString(object, "projectionMode", ToString(camera->GetProjectionMode()))));
            camera->SetVerticalFieldOfViewRadians(ReadFloat(object, "verticalFieldOfViewRadians", camera->GetVerticalFieldOfViewRadians()));
            camera->SetOrthographicSize(ReadFloat(object, "orthographicSize", camera->GetOrthographicSize()));
            camera->SetAspectRatio(ReadFloat(object, "aspectRatio", camera->GetAspectRatio()));
            if (ReadBool(object, "automaticAspectRatio", true))
            {
                camera->ResetAspectRatio();
            }
            camera->SetNearClipPlane(ReadFloat(object, "nearClipPlane", camera->GetNearClipPlane()));
            camera->SetFarClipPlane(ReadFloat(object, "farClipPlane", camera->GetFarClipPlane()));
            if (const boost::json::value* value = object.if_contains("clearColor"); value != nullptr)
            {
                Result<rhi::RhiColor> result = ReadColor(*value, "CameraComponent.clearColor");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                camera->SetClearColor(result.GetValue());
            }
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

        [[nodiscard]] ErrorCode ApplyColliderComponent(GameObject& gameObject, const boost::json::object& object)
        {
            ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>();
            if (collider == nullptr)
            {
                Result<ColliderComponent*> result = gameObject.AddComponentWithoutRenderRegistration<ColliderComponent>();
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                collider = result.GetValue();
            }

            ColliderDesc desc = collider->GetDesc();
            desc.shapeType = ParseColliderShapeType(ReadString(object, "shapeType", ToString(desc.shapeType)));
            desc.trigger = ReadBool(object, "isTrigger", desc.trigger);

            if (const boost::json::value* value = object.if_contains("center"); value != nullptr)
            {
                Result<Vector3> result = ReadVector3(*value, "ColliderComponent.center");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                desc.center = result.GetValue();
            }

            if (const boost::json::value* value = object.if_contains("size"); value != nullptr)
            {
                Result<Vector3> result = ReadVector3(*value, "ColliderComponent.size");
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                desc.size = result.GetValue();
            }

            desc.radius = ReadFloat(object, "radius", desc.radius);
            desc.height = ReadFloat(object, "height", desc.height);
            desc.direction = ParseColliderDirectionAxis(ReadString(object, "direction", ToString(desc.direction)));
            desc.staticFriction = ReadFloat(object, "staticFriction", desc.staticFriction);
            desc.dynamicFriction = ReadFloat(object, "dynamicFriction", desc.dynamicFriction);
            desc.bounciness = ReadFloat(object, "bounciness", desc.bounciness);
            collider->SetDesc(desc);
            collider->SetEnabled(ReadBool(object, "enabled", collider->IsEnabled()));
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ApplyRigidbodyComponent(GameObject& gameObject, const boost::json::object& object)
        {
            RigidbodyComponent* rigidbody = gameObject.GetComponent<RigidbodyComponent>();
            if (rigidbody == nullptr)
            {
                Result<RigidbodyComponent*> result = gameObject.AddComponentWithoutRenderRegistration<RigidbodyComponent>();
                if (!result)
                {
                    return result.GetError().GetCode();
                }
                rigidbody = result.GetValue();
            }

            RigidbodyDesc desc = rigidbody->GetDesc();
            desc.mass = ReadFloat(object, "mass", desc.mass);
            desc.linearDamping = ReadFloat(object, "linearDamping", desc.linearDamping);
            desc.angularDamping = ReadFloat(object, "angularDamping", desc.angularDamping);
            desc.useGravity = ReadBool(object, "useGravity", desc.useGravity);
            desc.kinematic = ReadBool(object, "isKinematic", desc.kinematic);
            desc.detectCollisions = ReadBool(object, "detectCollisions", desc.detectCollisions);
            desc.interpolationMode = ParseRigidbodyInterpolationMode(ReadString(object, "interpolation", ToString(desc.interpolationMode)));
            desc.collisionDetectionMode =
                ParseRigidbodyCollisionDetectionMode(ReadString(object, "collisionDetection", ToString(desc.collisionDetectionMode)));
            desc.constraints = ReadRigidbodyConstraints(object, "constraints", desc.constraints);
            rigidbody->SetDesc(desc);
            rigidbody->SetEnabled(ReadBool(object, "enabled", rigidbody->IsEnabled()));
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ApplyDotnetScriptableComponent(GameObject& gameObject, const boost::json::object& object, ScriptingSystem& scriptingSystem)
        {
            const std::string scriptTypeName = ReadString(object, "scriptTypeName");
            Result<DotnetScriptableComponent*> result =
                gameObject.AddComponentWithoutRenderRegistration<DotnetScriptableComponent>(scriptTypeName, scriptingSystem);
            if (!result)
            {
                return result.GetError().GetCode();
            }

            DotnetScriptableComponent* script = result.GetValue();
            script->SetEnabled(ReadBool(object, "enabled", script->IsEnabled()));

            if (scriptingSystem.GetBackendType() == ScriptingBackendType::Auto)
            {
                VE_LOG_WARN_CATEGORY("Scene", "Skipped script instance creation for '" + scriptTypeName + "' because scripting is not available on this platform.");
                return ErrorCode::None;
            }

            const ErrorCode ensureResult = script->EnsureScriptInstance(false);
            if (ensureResult != ErrorCode::None)
            {
                return ensureResult;
            }

            if (const boost::json::value* serializedFields = object.if_contains("serializedFields"); serializedFields != nullptr)
            {
                if (!serializedFields->is_object())
                {
                    return ErrorCode::InvalidArgument;
                }
                const ErrorCode fieldsResult = script->SetScriptFields(serializedFields->as_object());
                if (fieldsResult != ErrorCode::None)
                {
                    return fieldsResult;
                }
            }
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode ApplyComponents(GameObject& gameObject, const boost::json::array& components, ScriptingSystem& scriptingSystem)
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

            if (const boost::json::object* collider = FindComponent(components, "ColliderComponent"); collider != nullptr)
            {
                const ErrorCode result = ApplyColliderComponent(gameObject, *collider);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            if (const boost::json::object* rigidbody = FindComponent(components, "RigidbodyComponent"); rigidbody != nullptr)
            {
                const ErrorCode result = ApplyRigidbodyComponent(gameObject, *rigidbody);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }

            for (const boost::json::value& componentValue : components)
            {
                if (!componentValue.is_object())
                {
                    continue;
                }

                const boost::json::object& componentObject = componentValue.as_object();
                const std::string componentType = ReadString(componentObject, "type");
                if (componentType == "DotnetScriptableComponent" || componentType == "ScriptableComponent")
                {
                    const ErrorCode result = ApplyDotnetScriptableComponent(gameObject, componentObject, scriptingSystem);
                    if (result != ErrorCode::None)
                    {
                        return result;
                    }
                }
            }

            return ErrorCode::None;
        }
    } // namespace

    ErrorCode SceneSerialization::ReadGameObjectRecursive(Scene& scene,
                                                          TransformComponent* parent,
                                                          const boost::json::object& object,
                                                          ScriptingSystem& scriptingSystem)
    {
        const std::string name = ReadString(object, "name");

        GameObject* gameObject = nullptr;
        if (parent == nullptr)
        {
            Result<GameObject*> result = scene.CreateRootGameObject(name);
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

            const ErrorCode result = ApplyComponents(*gameObject, componentsValue->as_array(), scriptingSystem);
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

                const ErrorCode result = ReadGameObjectRecursive(scene, transform, childValue.as_object(), scriptingSystem);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }
        }

        return ErrorCode::None;
    }

    ErrorCode SceneSerialization::ReadScene(Scene& scene, const boost::json::object& object, ScriptingSystem& scriptingSystem)
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

            const ErrorCode result = ReadGameObjectRecursive(scene, nullptr, rootValue.as_object(), scriptingSystem);
            if (result != ErrorCode::None)
            {
                return result;
            }
        }

        return ErrorCode::None;
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

    ErrorCode SceneSerialization::LoadFromString(Scene& scene, std::string_view text, ScriptingSystem& scriptingSystem)
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

        return ReadScene(scene, value.as_object(), scriptingSystem);
    }
} // namespace ve
