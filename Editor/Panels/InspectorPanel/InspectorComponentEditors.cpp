#include "Editor/Core/Editor.h"
#include "Editor/Panels/InspectorPanel/InspectorPanel.h"
#include "Editor/Panels/InspectorPanel/InspectorPanelInternal.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/ColliderComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/RigidbodyComponent.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"

#include <algorithm>
#include <array>
#include <boost/json.hpp>
#include <imgui.h>
#include <string>
#include <vector>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] Float32 ReadJsonFloat(const boost::json::value& value, Float32 fallback = 0.0f) noexcept
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

            return fallback;
        }

        [[nodiscard]] boost::json::value GetDefaultScriptFieldValue(const ScriptFieldInfo& field)
        {
            if (!field.defaultValueJson.empty())
            {
                Result<boost::json::value> defaultValue = JsonUtils::Parse(field.defaultValueJson);
                if (defaultValue)
                {
                    return defaultValue.MoveValue();
                }
            }

            switch (field.kind)
            {
            case ScriptFieldKind::Bool:
                return false;
            case ScriptFieldKind::Int:
                return 0;
            case ScriptFieldKind::Float:
                return 0.0;
            case ScriptFieldKind::String:
                return boost::json::string();
            case ScriptFieldKind::Vector3:
                return boost::json::object{{"X", 0.0}, {"Y", 0.0}, {"Z", 0.0}};
            case ScriptFieldKind::Color:
                return boost::json::object{{"R", 0.0}, {"G", 0.0}, {"B", 0.0}, {"A", 1.0}};
            case ScriptFieldKind::Enum:
                return boost::json::string(field.enumNames.empty() ? "" : field.enumNames.front());
            case ScriptFieldKind::Unsupported:
                break;
            }

            return nullptr;
        }

        [[nodiscard]] boost::json::value GetScriptFieldValue(const boost::json::object& fields, const ScriptFieldInfo& field)
        {
            if (const boost::json::value* value = fields.if_contains(field.name); value != nullptr)
            {
                return boost::json::value(*value);
            }

            return GetDefaultScriptFieldValue(field);
        }

        [[nodiscard]] std::array<float, 3> ReadScriptVector3(const boost::json::value& value)
        {
            if (value.is_object())
            {
                const boost::json::object& object = value.as_object();
                const boost::json::value* x = object.if_contains("X");
                const boost::json::value* y = object.if_contains("Y");
                const boost::json::value* z = object.if_contains("Z");
                return {x != nullptr ? ReadJsonFloat(*x) : 0.0f, y != nullptr ? ReadJsonFloat(*y) : 0.0f, z != nullptr ? ReadJsonFloat(*z) : 0.0f};
            }

            if (value.is_array() && value.as_array().size() == 3)
            {
                const boost::json::array& array = value.as_array();
                return {ReadJsonFloat(array[0]), ReadJsonFloat(array[1]), ReadJsonFloat(array[2])};
            }

            return {0.0f, 0.0f, 0.0f};
        }

        [[nodiscard]] boost::json::object WriteScriptVector3(const std::array<float, 3>& value)
        {
            return boost::json::object{{"X", value[0]}, {"Y", value[1]}, {"Z", value[2]}};
        }

        [[nodiscard]] std::array<float, 4> ReadScriptColor(const boost::json::value& value)
        {
            if (value.is_object())
            {
                const boost::json::object& object = value.as_object();
                const boost::json::value* r = object.if_contains("R");
                const boost::json::value* g = object.if_contains("G");
                const boost::json::value* b = object.if_contains("B");
                const boost::json::value* a = object.if_contains("A");
                return {r != nullptr ? ReadJsonFloat(*r) : 0.0f,
                        g != nullptr ? ReadJsonFloat(*g) : 0.0f,
                        b != nullptr ? ReadJsonFloat(*b) : 0.0f,
                        a != nullptr ? ReadJsonFloat(*a, 1.0f) : 1.0f};
            }

            if (value.is_array() && value.as_array().size() == 4)
            {
                const boost::json::array& array = value.as_array();
                return {ReadJsonFloat(array[0]), ReadJsonFloat(array[1]), ReadJsonFloat(array[2]), ReadJsonFloat(array[3], 1.0f)};
            }

            return {0.0f, 0.0f, 0.0f, 1.0f};
        }

        [[nodiscard]] boost::json::object WriteScriptColor(const std::array<float, 4>& value)
        {
            return boost::json::object{{"R", value[0]}, {"G", value[1]}, {"B", value[2]}, {"A", value[3]}};
        }

        void SetScriptField(DotnetScriptableComponent& script, const ScriptFieldInfo& field, const boost::json::value& value)
        {
            const ErrorCode result = script.SetScriptField(field.name, value);
            if (result != ErrorCode::None)
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to set script field '{}.{}': {}", script.GetScriptTypeName(), field.name, ToString(result));
            }
        }

        void RenderScriptField(DotnetScriptableComponent& script, const boost::json::object& fields, const ScriptFieldInfo& field)
        {
            ImGui::PushID(field.name.c_str());
            const std::string& label = field.displayName.empty() ? field.name : field.displayName;
            boost::json::value value = GetScriptFieldValue(fields, field);

            switch (field.kind)
            {
            case ScriptFieldKind::Bool:
            {
                bool boolValue = value.is_bool() ? value.as_bool() : false;
                if (RenderFieldCheckbox(label.c_str(), &boolValue))
                {
                    SetScriptField(script, field, boolValue);
                }
                break;
            }
            case ScriptFieldKind::Int:
            {
                int intValue = static_cast<int>(ReadJsonFloat(value));
                if (RenderFieldDragInt(label.c_str(), &intValue))
                {
                    SetScriptField(script, field, intValue);
                }
                break;
            }
            case ScriptFieldKind::Float:
            {
                float floatValue = ReadJsonFloat(value);
                if (RenderFieldDragFloat(label.c_str(), &floatValue, FineDragSpeed))
                {
                    SetScriptField(script, field, floatValue);
                }
                break;
            }
            case ScriptFieldKind::String:
            {
                std::array<char, TextBufferSize> buffer = ToTextBuffer(value.is_string() ? std::string(value.as_string()) : std::string());
                if (RenderFieldInputText(label.c_str(), buffer.data(), buffer.size()))
                {
                    SetScriptField(script, field, boost::json::string(buffer.data()));
                }
                break;
            }
            case ScriptFieldKind::Vector3:
            {
                std::array<float, 3> vectorValue = ReadScriptVector3(value);
                if (RenderFieldDragFloat3(label.c_str(), vectorValue.data(), TransformDragSpeed))
                {
                    SetScriptField(script, field, WriteScriptVector3(vectorValue));
                }
                break;
            }
            case ScriptFieldKind::Color:
            {
                std::array<float, 4> colorValue = ReadScriptColor(value);
                if (RenderFieldColorEdit4(label.c_str(), colorValue.data()))
                {
                    SetScriptField(script, field, WriteScriptColor(colorValue));
                }
                break;
            }
            case ScriptFieldKind::Enum:
            {
                int selectedIndex = 0;
                std::string selectedName;
                if (value.is_string())
                {
                    selectedName = std::string(value.as_string());
                }
                for (SizeT index = 0; index < field.enumNames.size(); ++index)
                {
                    if (field.enumNames[index] == selectedName)
                    {
                        selectedIndex = static_cast<int>(index);
                        break;
                    }
                }

                std::vector<const char*> enumLabels;
                enumLabels.reserve(field.enumNames.size());
                for (const std::string& enumName : field.enumNames)
                {
                    enumLabels.push_back(enumName.c_str());
                }

                if (!enumLabels.empty() && RenderFieldCombo(label.c_str(), &selectedIndex, enumLabels.data(), static_cast<int>(enumLabels.size())))
                {
                    SetScriptField(script, field, boost::json::string(field.enumNames[static_cast<SizeT>(selectedIndex)]));
                }
                break;
            }
            case ScriptFieldKind::Unsupported:
                break;
            }

            ImGui::PopID();
        }
    } // namespace

    void InspectorPanel::RenderTransformComponent(TransformComponent& transform)
    {
        ImGui::PushID(&transform);
        if (RenderComponentHeader("Transform", ImVec4(0.18f, 0.42f, 0.50f, 0.82f)))
        {
            RenderEnabledCheckbox(transform);

            std::array<float, 3> position = ToFloat3(transform.GetLocalPosition());
            if (RenderFieldDragFloat3("Position", position.data(), TransformDragSpeed))
            {
                transform.SetLocalPosition(FromFloat3(position));
            }

            std::array<float, 4> rotation = ToFloat4(transform.GetLocalRotation());
            if (RenderFieldDragFloat4("Rotation XYZW", rotation.data(), RotationDragSpeed))
            {
                transform.SetLocalRotation(FromFloat4(rotation));
            }

            std::array<float, 3> scale = ToFloat3(transform.GetLocalScale());
            if (RenderFieldDragFloat3("Scale", scale.data(), TransformDragSpeed))
            {
                transform.SetLocalScale(FromFloat3(scale));
            }
        }
        ImGui::PopID();
    }

    bool InspectorPanel::RenderMeshRenderComponent(GameObject& gameObject, MeshRenderComponent& mesh)
    {
        ImGui::PushID(&mesh);
        const bool open = RenderComponentHeader("Mesh Renderer", ImVec4(0.22f, 0.32f, 0.58f, 0.82f));
        if (RenderRemoveComponentContextMenu<MeshRenderComponent>(gameObject, "MeshRenderComponentContext"))
        {
            ImGui::PopID();
            return true;
        }

        if (open)
        {
            RenderEnabledCheckbox(mesh);

            const EditorAssetDatabase* assetDatabase = editor_ != nullptr ? &editor_->GetAssetDatabase() : nullptr;
            const std::string meshAssetPath = assetDatabase != nullptr ? ResolveAssetPathFromID(*assetDatabase, mesh.GetMeshAssetID()) : "";
            RenderAssetReferenceField("Mesh", "##MeshReferencePath", "##MeshReference", meshAssetPath);

            const std::string materialAssetPath = assetDatabase != nullptr ? ResolveAssetPathFromID(*assetDatabase, mesh.GetMaterialAssetID()) : "";
            RenderAssetReferenceField("Material", "##MaterialReferencePath", "##MaterialReference", materialAssetPath);

            bool castShadows = mesh.CastShadows();
            if (RenderFieldCheckbox("Cast Shadows", &castShadows))
            {
                mesh.SetCastShadows(castShadows);
            }

            bool receiveShadows = mesh.ReceiveShadows();
            if (RenderFieldCheckbox("Receive Shadows", &receiveShadows))
            {
                mesh.SetReceiveShadows(receiveShadows);
            }

            std::array<float, 3> boundsCenter = ToFloat3(mesh.GetBoundsCenter());
            if (RenderFieldDragFloat3("Bounds Center", boundsCenter.data(), BoundsDragSpeed))
            {
                mesh.SetBoundsCenter(FromFloat3(boundsCenter));
            }

            std::array<float, 3> boundsExtents = ToFloat3(mesh.GetBoundsExtents());
            if (RenderFieldDragFloat3("Bounds Extents", boundsExtents.data(), BoundsDragSpeed))
            {
                mesh.SetBoundsExtents(FromFloat3(boundsExtents));
            }
        }
        ImGui::PopID();
        return false;
    }

    bool InspectorPanel::RenderCameraComponent(GameObject& gameObject, CameraComponent& camera)
    {
        ImGui::PushID(&camera);
        const bool open = RenderComponentHeader("Camera", ImVec4(0.20f, 0.46f, 0.30f, 0.82f));
        if (RenderRemoveComponentContextMenu<CameraComponent>(gameObject, "CameraComponentContext"))
        {
            ImGui::PopID();
            return true;
        }

        if (open)
        {
            RenderEnabledCheckbox(camera);

            int projectionMode = camera.GetProjectionMode() == CameraComponent::ProjectionMode::Perspective ? 0 : 1;
            const char* projectionModes[] = {"Perspective", "Orthographic"};
            if (RenderFieldCombo("Projection", &projectionMode, projectionModes, IM_ARRAYSIZE(projectionModes)))
            {
                camera.SetProjectionMode(projectionMode == 0 ? CameraComponent::ProjectionMode::Perspective : CameraComponent::ProjectionMode::Orthographic);
            }

            float verticalFieldOfView = camera.GetVerticalFieldOfViewRadians();
            if (RenderFieldDragFloat("FOV Radians", &verticalFieldOfView, FineDragSpeed))
            {
                camera.SetVerticalFieldOfViewRadians(verticalFieldOfView);
            }

            float orthographicSize = camera.GetOrthographicSize();
            if (RenderFieldDragFloat("Ortho Size", &orthographicSize, MediumDragSpeed))
            {
                camera.SetOrthographicSize(orthographicSize);
            }

            float aspectRatio = camera.GetAspectRatio();
            if (RenderFieldDragFloat("Aspect Override", &aspectRatio, FineDragSpeed))
            {
                camera.SetAspectRatio(aspectRatio);
            }

            bool automaticAspectRatio = camera.IsAspectRatioAutomatic();
            if (RenderFieldCheckbox("Auto Aspect", &automaticAspectRatio))
            {
                if (automaticAspectRatio)
                {
                    camera.ResetAspectRatio();
                }
                else
                {
                    camera.SetAspectRatio(aspectRatio);
                }
            }

            float nearClipPlane = camera.GetNearClipPlane();
            if (RenderFieldDragFloat("Near", &nearClipPlane, FineDragSpeed))
            {
                camera.SetNearClipPlane(nearClipPlane);
            }

            float farClipPlane = camera.GetFarClipPlane();
            if (RenderFieldDragFloat("Far", &farClipPlane, LargeDragSpeed))
            {
                camera.SetFarClipPlane(farClipPlane);
            }

            std::array<float, 4> clearColor = ToFloat4(camera.GetClearColor());
            if (RenderFieldColorEdit4("Clear Color", clearColor.data()))
            {
                camera.SetClearColor(ToRhiColor(clearColor));
            }
        }
        ImGui::PopID();
        return false;
    }

    bool InspectorPanel::RenderLightComponent(GameObject& gameObject, LightComponent& light)
    {
        ImGui::PushID(&light);
        const bool open = RenderComponentHeader("Light", ImVec4(0.54f, 0.42f, 0.18f, 0.82f));
        if (RenderRemoveComponentContextMenu<LightComponent>(gameObject, "LightComponentContext"))
        {
            ImGui::PopID();
            return true;
        }

        if (open)
        {
            RenderEnabledCheckbox(light);

            int lightType = light.GetLightType() == LightType::Directional ? 0 : 1;
            const char* lightTypes[] = {"Directional", "Point"};
            if (RenderFieldCombo("Type", &lightType, lightTypes, IM_ARRAYSIZE(lightTypes)))
            {
                light.SetLightType(lightType == 0 ? LightType::Directional : LightType::Point);
            }

            std::array<float, 3> color = ToFloat3(light.GetColor());
            if (RenderFieldColorEdit3("Color", color.data()))
            {
                light.SetColor(FromFloat3(color));
            }

            float intensity = light.GetIntensity();
            if (RenderFieldDragFloat("Intensity", &intensity, MediumDragSpeed))
            {
                light.SetIntensity(intensity);
            }

            float range = light.GetRange();
            if (RenderFieldDragFloat("Range", &range, MediumDragSpeed))
            {
                light.SetRange(range);
            }

            float innerConeAngle = light.GetInnerConeAngleRadians();
            if (RenderFieldDragFloat("Inner Cone", &innerConeAngle, FineDragSpeed))
            {
                light.SetInnerConeAngleRadians(innerConeAngle);
            }

            float outerConeAngle = light.GetOuterConeAngleRadians();
            if (RenderFieldDragFloat("Outer Cone", &outerConeAngle, FineDragSpeed))
            {
                light.SetOuterConeAngleRadians(outerConeAngle);
            }

            bool castShadows = light.CastShadows();
            if (RenderFieldCheckbox("Cast Shadows", &castShadows))
            {
                light.SetCastShadows(castShadows);
            }
        }
        ImGui::PopID();
        return false;
    }

    bool InspectorPanel::RenderColliderComponent(GameObject& gameObject, ColliderComponent& collider)
    {
        ImGui::PushID(&collider);
        const bool open = RenderComponentHeader("Collider", ImVec4(0.43f, 0.28f, 0.55f, 0.82f));
        if (RenderRemoveComponentContextMenu<ColliderComponent>(gameObject, "ColliderComponentContext"))
        {
            ImGui::PopID();
            return true;
        }

        if (open)
        {
            RenderEnabledCheckbox(collider);

            int shapeType = std::clamp(static_cast<int>(collider.GetShapeType()), 0, 2);
            const char* shapeTypes[] = {"Box", "Sphere", "Capsule"};
            if (RenderFieldCombo("Shape", &shapeType, shapeTypes, IM_ARRAYSIZE(shapeTypes)))
            {
                collider.SetShapeType(static_cast<ColliderShapeType>(shapeType));
            }

            bool trigger = collider.IsTrigger();
            if (RenderFieldCheckbox("Is Trigger", &trigger))
            {
                collider.SetTrigger(trigger);
            }

            std::array<float, 3> center = ToFloat3(collider.GetCenter());
            if (RenderFieldDragFloat3("Center", center.data(), BoundsDragSpeed))
            {
                collider.SetCenter(FromFloat3(center));
            }

            std::array<float, 3> size = ToFloat3(collider.GetSize());
            if (RenderFieldDragFloat3("Size", size.data(), BoundsDragSpeed))
            {
                collider.SetSize(FromFloat3(size));
            }

            float radius = collider.GetRadius();
            if (RenderFieldDragFloat("Radius", &radius, FineDragSpeed))
            {
                collider.SetRadius(radius);
            }

            float height = collider.GetHeight();
            if (RenderFieldDragFloat("Height", &height, FineDragSpeed))
            {
                collider.SetHeight(height);
            }

            int direction = std::clamp(static_cast<int>(collider.GetDirection()), 0, 2);
            const char* directions[] = {"X", "Y", "Z"};
            if (RenderFieldCombo("Direction", &direction, directions, IM_ARRAYSIZE(directions)))
            {
                collider.SetDirection(static_cast<ColliderDirectionAxis>(direction));
            }

            float staticFriction = collider.GetStaticFriction();
            if (RenderFieldDragFloat("Static Friction", &staticFriction, FineDragSpeed))
            {
                collider.SetStaticFriction(staticFriction);
            }

            float dynamicFriction = collider.GetDynamicFriction();
            if (RenderFieldDragFloat("Dynamic Friction", &dynamicFriction, FineDragSpeed))
            {
                collider.SetDynamicFriction(dynamicFriction);
            }

            float bounciness = collider.GetBounciness();
            if (RenderFieldDragFloat("Bounciness", &bounciness, FineDragSpeed))
            {
                collider.SetBounciness(bounciness);
            }
        }
        ImGui::PopID();
        return false;
    }

    bool InspectorPanel::RenderRigidbodyComponent(GameObject& gameObject, RigidbodyComponent& rigidbody)
    {
        ImGui::PushID(&rigidbody);
        const bool open = RenderComponentHeader("Rigidbody", ImVec4(0.48f, 0.25f, 0.22f, 0.82f));
        if (RenderRemoveComponentContextMenu<RigidbodyComponent>(gameObject, "RigidbodyComponentContext"))
        {
            ImGui::PopID();
            return true;
        }

        if (open)
        {
            RenderEnabledCheckbox(rigidbody);

            float mass = rigidbody.GetMass();
            if (RenderFieldDragFloat("Mass", &mass, FineDragSpeed))
            {
                rigidbody.SetMass(mass);
            }

            float linearDamping = rigidbody.GetLinearDamping();
            if (RenderFieldDragFloat("Linear Damping", &linearDamping, FineDragSpeed))
            {
                rigidbody.SetLinearDamping(linearDamping);
            }

            float angularDamping = rigidbody.GetAngularDamping();
            if (RenderFieldDragFloat("Angular Damping", &angularDamping, FineDragSpeed))
            {
                rigidbody.SetAngularDamping(angularDamping);
            }

            bool useGravity = rigidbody.UsesGravity();
            if (RenderFieldCheckbox("Use Gravity", &useGravity))
            {
                rigidbody.SetUseGravity(useGravity);
            }

            bool kinematic = rigidbody.IsKinematic();
            if (RenderFieldCheckbox("Is Kinematic", &kinematic))
            {
                rigidbody.SetKinematic(kinematic);
            }

            bool detectCollisions = rigidbody.DetectsCollisions();
            if (RenderFieldCheckbox("Detect Collisions", &detectCollisions))
            {
                rigidbody.SetDetectCollisions(detectCollisions);
            }

            int interpolationMode = std::clamp(static_cast<int>(rigidbody.GetInterpolationMode()), 0, 2);
            const char* interpolationModes[] = {"None", "Interpolate", "Extrapolate"};
            if (RenderFieldCombo("Interpolation", &interpolationMode, interpolationModes, IM_ARRAYSIZE(interpolationModes)))
            {
                rigidbody.SetInterpolationMode(static_cast<RigidbodyInterpolationMode>(interpolationMode));
            }

            int collisionDetectionMode = std::clamp(static_cast<int>(rigidbody.GetCollisionDetectionMode()), 0, 3);
            const char* collisionDetectionModes[] = {"Discrete", "Continuous", "Continuous Dynamic", "Continuous Speculative"};
            if (RenderFieldCombo("Collision Detection", &collisionDetectionMode, collisionDetectionModes, IM_ARRAYSIZE(collisionDetectionModes)))
            {
                rigidbody.SetCollisionDetectionMode(static_cast<RigidbodyCollisionDetectionMode>(collisionDetectionMode));
            }

            ImGui::SeparatorText("Constraints");
            RenderConstraintCheckbox(rigidbody, "Freeze Position X", RigidbodyConstraintFlags::FreezePositionX);
            RenderConstraintCheckbox(rigidbody, "Freeze Position Y", RigidbodyConstraintFlags::FreezePositionY);
            RenderConstraintCheckbox(rigidbody, "Freeze Position Z", RigidbodyConstraintFlags::FreezePositionZ);
            RenderConstraintCheckbox(rigidbody, "Freeze Rotation X", RigidbodyConstraintFlags::FreezeRotationX);
            RenderConstraintCheckbox(rigidbody, "Freeze Rotation Y", RigidbodyConstraintFlags::FreezeRotationY);
            RenderConstraintCheckbox(rigidbody, "Freeze Rotation Z", RigidbodyConstraintFlags::FreezeRotationZ);
        }
        ImGui::PopID();
        return false;
    }

    bool InspectorPanel::RenderScriptComponent(GameObject& gameObject, DotnetScriptableComponent& script)
    {
        ImGui::PushID(&script);
        const bool open = RenderComponentHeader("Script", ImVec4(0.36f, 0.34f, 0.56f, 0.82f));
        if (ImGui::BeginPopupContextItem("ScriptComponentContext"))
        {
            if (ImGui::MenuItem("Remove Component"))
            {
                const bool removed = gameObject.RemoveScriptableComponent(script);
                ImGui::EndPopup();
                ImGui::PopID();
                return removed;
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            RenderEnabledCheckbox(script);

            const ScriptTypeInfo* scriptType = editor_ != nullptr ? editor_->GetScriptDatabase().FindScriptType(script.GetScriptTypeName()) : nullptr;
            if (scriptType == nullptr)
            {
                ImGui::TextDisabled("Missing Script: %s", script.GetScriptTypeName().c_str());
            }
            else
            {
                ImGui::Text("Type: %s", scriptType->typeName.c_str());
                boost::json::object fields;
                const ErrorCode ensureResult = script.EnsureScriptInstance(false);
                if (ensureResult != ErrorCode::None)
                {
                    ImGui::TextDisabled("Managed Instance: Failed");
                    VE_LOG_WARN_CATEGORY("Editor", "Failed to create editor script instance '{}': {}", script.GetScriptTypeName(), ToString(ensureResult));
                }
                else
                {
                    Result<boost::json::object> fieldResult = script.GetScriptFields();
                    if (fieldResult)
                    {
                        fields = fieldResult.MoveValue();
                    }
                    else
                    {
                        VE_LOG_WARN_CATEGORY(
                            "Editor", "Failed to read script fields '{}': {}", script.GetScriptTypeName(), fieldResult.GetError().GetMessage());
                    }
                }

                for (const ScriptFieldInfo& field : scriptType->fields)
                {
                    RenderScriptField(script, fields, field);
                }
            }

            ImGui::Text("Managed Instance: %s", script.HasScriptInstance() ? "Yes" : "No");
        }
        ImGui::PopID();
        return false;
    }
} // namespace ve::editor
