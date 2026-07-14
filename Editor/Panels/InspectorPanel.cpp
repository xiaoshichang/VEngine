#include "Editor/Panels/InspectorPanel.h"

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorAssetDatabase.h"
#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Resource/MaterialProperty.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/ColliderComponent.h"
#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/RigidbodyComponent.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"

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
        constexpr SizeT TextBufferSize = 512;
        constexpr float TransformDragSpeed = 0.05f;
        constexpr float RotationDragSpeed = 0.01f;
        constexpr float BoundsDragSpeed = 0.05f;
        constexpr float FineDragSpeed = 0.01f;
        constexpr float MediumDragSpeed = 0.1f;
        constexpr float LargeDragSpeed = 1.0f;

        [[nodiscard]] std::array<char, TextBufferSize> ToTextBuffer(const std::string& value)
        {
            std::array<char, TextBufferSize> buffer{};
            const size_t copySize = (std::min)(value.size(), buffer.size() - 1);
            value.copy(buffer.data(), copySize);
            buffer[copySize] = '\0';
            return buffer;
        }

        [[nodiscard]] std::array<float, 3> ToFloat3(const Vector3& value) noexcept
        {
            return {value.GetX(), value.GetY(), value.GetZ()};
        }

        [[nodiscard]] Vector3 FromFloat3(const std::array<float, 3>& value) noexcept
        {
            return Vector3(value[0], value[1], value[2]);
        }

        [[nodiscard]] std::array<float, 4> ToFloat4(const Quaternion& value) noexcept
        {
            return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
        }

        [[nodiscard]] std::array<float, 4> ToFloat4(const rhi::RhiColor& value) noexcept
        {
            return {value.r, value.g, value.b, value.a};
        }

        [[nodiscard]] Quaternion FromFloat4(const std::array<float, 4>& value) noexcept
        {
            return Quaternion(value[0], value[1], value[2], value[3]).Normalized();
        }

        [[nodiscard]] rhi::RhiColor ToRhiColor(const std::array<float, 4>& value) noexcept
        {
            return rhi::RhiColor{value[0], value[1], value[2], value[3]};
        }

        [[nodiscard]] std::array<float, 4> ToFloat4(const Vector4& value) noexcept
        {
            return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
        }

        [[nodiscard]] Vector4 FromFloat4Vector(const std::array<float, 4>& value) noexcept
        {
            return Vector4(value[0], value[1], value[2], value[3]);
        }

        bool RenderEnabledCheckbox(Component& component)
        {
            bool enabled = component.IsEnabled();
            if (ImGui::Checkbox("Enabled", &enabled))
            {
                component.SetEnabled(enabled);
                return true;
            }

            return false;
        }

        void RenderConstraintCheckbox(RigidbodyComponent& rigidbody, const char* label, RigidbodyConstraintFlags flag)
        {
            bool enabled = rigidbody.HasConstraint(flag);
            if (ImGui::Checkbox(label, &enabled))
            {
                RigidbodyConstraintFlags constraints = rigidbody.GetConstraints();
                if (enabled)
                {
                    constraints |= flag;
                }
                else
                {
                    constraints &= ~flag;
                }
                rigidbody.SetConstraints(constraints);
            }
        }

        [[nodiscard]] std::string ResolveAssetPathFromID(const EditorAssetDatabase& assetDatabase, const AssetID& id)
        {
            if (id.IsEmpty())
            {
                return {};
            }

            const EditorAssetRecord* asset = assetDatabase.FindAssetByID(id);
            if (asset != nullptr)
            {
                return asset->path.GetString();
            }

            return "Missing asset: " + id.ToString();
        }

        void RenderAssetReferenceField(const char* label, const char* pathInputId, const char* buttonId, const std::string& assetPath)
        {
            std::array<char, TextBufferSize> pathBuffer = ToTextBuffer(assetPath);
            const ImGuiStyle& style = ImGui::GetStyle();
            const float buttonWidth = ImGui::CalcTextSize("...").x + style.FramePadding.x * 2.0f;

            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-buttonWidth - style.ItemSpacing.x);
            ImGui::InputText(pathInputId, pathBuffer.data(), pathBuffer.size(), ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button(buttonId))
            {
            }
        }

        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] Path ResolveProjectRelativeReference(std::string_view reference, std::string_view fallbackExtension)
        {
            if (reference.empty())
            {
                return Path();
            }

            std::string path(reference);
            if (!path.starts_with("Assets/"))
            {
                path = "Assets/" + path;
            }

            if (Path(path).GetExtension().empty() && !fallbackExtension.empty())
            {
                path += fallbackExtension;
            }

            return Path(path);
        }

        [[nodiscard]] Result<boost::json::object> ReadJsonObjectFile(const Path& path)
        {
            Result<std::string> text = FileSystem::ReadTextFile(path);
            if (!text)
            {
                return Result<boost::json::object>::Failure(text.GetError());
            }

            Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
            if (!json || !json.GetValue().is_object())
            {
                return Result<boost::json::object>::Failure(Error(ErrorCode::InvalidArgument, "JSON root must be an object."));
            }

            return Result<boost::json::object>::Success(json.GetValue().as_object());
        }

        [[nodiscard]] MaterialResource* FindLoadedMaterialResource(ResourceSystem& resourceSystem, const AssetID& id)
        {
            ResourceObject* resource = resourceSystem.FindLoadedResource(id);
            if (resource == nullptr || resource->GetType() != ResourceType::Material)
            {
                return nullptr;
            }

            return dynamic_cast<MaterialResource*>(resource);
        }

        [[nodiscard]] Result<AssetID> ParseAssetIDText(const std::string& text)
        {
            if (text.empty())
            {
                return Result<AssetID>::Success(AssetID());
            }

            const size_t separator = text.find(':');
            const std::string guidText = separator == std::string::npos ? text : text.substr(0, separator);
            UInt64 subID = 0;
            if (separator != std::string::npos && separator + 1 < text.size())
            {
                try
                {
                    subID = static_cast<UInt64>(std::stoull(text.substr(separator + 1)));
                }
                catch (const std::exception&)
                {
                    return Result<AssetID>::Failure(Error(ErrorCode::InvalidArgument, "Invalid AssetID subID."));
                }
            }

            Result<Guid> guid = Guid::Parse(guidText);
            if (!guid)
            {
                return Result<AssetID>::Failure(guid.GetError());
            }

            return Result<AssetID>::Success(AssetID(guid.GetValue(), subID));
        }

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
                if (ImGui::Checkbox(label.c_str(), &boolValue))
                {
                    SetScriptField(script, field, boolValue);
                }
                break;
            }
            case ScriptFieldKind::Int:
            {
                int intValue = static_cast<int>(ReadJsonFloat(value));
                if (ImGui::DragInt(label.c_str(), &intValue))
                {
                    SetScriptField(script, field, intValue);
                }
                break;
            }
            case ScriptFieldKind::Float:
            {
                float floatValue = ReadJsonFloat(value);
                if (ImGui::DragFloat(label.c_str(), &floatValue, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
                {
                    SetScriptField(script, field, floatValue);
                }
                break;
            }
            case ScriptFieldKind::String:
            {
                std::array<char, TextBufferSize> buffer = ToTextBuffer(value.is_string() ? std::string(value.as_string()) : std::string());
                if (ImGui::InputText(label.c_str(), buffer.data(), buffer.size()))
                {
                    SetScriptField(script, field, boost::json::string(buffer.data()));
                }
                break;
            }
            case ScriptFieldKind::Vector3:
            {
                std::array<float, 3> vectorValue = ReadScriptVector3(value);
                if (ImGui::DragFloat3(label.c_str(), vectorValue.data(), TransformDragSpeed, 0.0f, 0.0f, "%.3f"))
                {
                    SetScriptField(script, field, WriteScriptVector3(vectorValue));
                }
                break;
            }
            case ScriptFieldKind::Color:
            {
                std::array<float, 4> colorValue = ReadScriptColor(value);
                if (ImGui::ColorEdit4(label.c_str(), colorValue.data()))
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

                if (!enumLabels.empty() && ImGui::Combo(label.c_str(), &selectedIndex, enumLabels.data(), static_cast<int>(enumLabels.size())))
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

    void InspectorPanel::Init(Editor& editor)
    {
        editor_ = &editor;
        selection_ = editor.GetSelection();
        if (!selectionSubscription_.IsValid())
        {
            selectionSubscription_ = editor.GetEventDispatcher().Subscribe<EditorSelectionChangedEvent>([this](const EditorSelectionChangedEvent& event)
                                                                                                        { OnSelectionChanged(event); });
        }
    }

    const char* InspectorPanel::GetName() const noexcept
    {
        return "Inspector";
    }

    void InspectorPanel::RenderContent()
    {
        if (editor_ == nullptr)
        {
            return;
        }

        switch (selection_.selectionType)
        {
        case EditorSelectionType::GameObject:
        {
            GameObject* selectedGameObject = selection_.gameObject;
            if (selectedGameObject != nullptr)
            {
                RenderGameObject(*selectedGameObject);
            }
            return;
        }
        case EditorSelectionType::Asset:
        {
            const EditorAssetRecord* asset = editor_->GetAssetDatabase().FindAsset(selection_.assetPath);
            if (asset != nullptr)
            {
                RenderAsset(*asset);
            }
            else
            {
                ImGui::TextDisabled("Selected asset was not found.");
            }
            return;
        }
        case EditorSelectionType::None:
            break;
        }

        ImGui::TextDisabled("Nothing selected.");
    }

    void InspectorPanel::OnSelectionChanged(const EditorSelectionChangedEvent& event)
    {
        selection_ = event;
    }

    void InspectorPanel::RenderGameObject(GameObject& gameObject)
    {
        std::array<char, TextBufferSize> nameBuffer = ToTextBuffer(gameObject.GetName());
        if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
        {
            gameObject.SetName(nameBuffer.data());
        }

        ImGui::TextDisabled("%zu component(s)", static_cast<size_t>(gameObject.GetComponentCount()));
        ImGui::Separator();

        if (TransformComponent* transform = gameObject.GetComponent<TransformComponent>(); transform != nullptr)
        {
            RenderTransformComponent(*transform);
        }

        if (MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>(); mesh != nullptr)
        {
            RenderMeshRenderComponent(*mesh);
        }

        if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr)
        {
            RenderCameraComponent(*camera);
        }

        if (LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr)
        {
            RenderLightComponent(*light);
        }

        if (ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>(); collider != nullptr)
        {
            RenderColliderComponent(gameObject, *collider);
        }

        if (RigidbodyComponent* rigidbody = gameObject.GetComponent<RigidbodyComponent>(); rigidbody != nullptr)
        {
            RenderRigidbodyComponent(gameObject, *rigidbody);
        }

        for (SizeT scriptIndex = 0; scriptIndex < gameObject.GetScriptableComponentCount();)
        {
            ScriptableComponent* script = gameObject.GetScriptableComponent(scriptIndex);
            DotnetScriptableComponent* dotnetScript = dynamic_cast<DotnetScriptableComponent*>(script);
            if (dotnetScript == nullptr)
            {
                ++scriptIndex;
                continue;
            }

            if (RenderScriptComponent(gameObject, *dotnetScript))
            {
                continue;
            }

            ++scriptIndex;
        }

        RenderAddComponent(gameObject);
    }

    void InspectorPanel::RenderTransformComponent(TransformComponent& transform)
    {
        ImGui::PushID(&transform);
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderEnabledCheckbox(transform);

            std::array<float, 3> position = ToFloat3(transform.GetLocalPosition());
            if (ImGui::DragFloat3("Position", position.data(), TransformDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                transform.SetLocalPosition(FromFloat3(position));
            }

            std::array<float, 4> rotation = ToFloat4(transform.GetLocalRotation());
            if (ImGui::DragFloat4("Rotation XYZW", rotation.data(), RotationDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                transform.SetLocalRotation(FromFloat4(rotation));
            }

            std::array<float, 3> scale = ToFloat3(transform.GetLocalScale());
            if (ImGui::DragFloat3("Scale", scale.data(), TransformDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                transform.SetLocalScale(FromFloat3(scale));
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderMeshRenderComponent(MeshRenderComponent& mesh)
    {
        ImGui::PushID(&mesh);
        if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderEnabledCheckbox(mesh);

            const EditorAssetDatabase* assetDatabase = editor_ != nullptr ? &editor_->GetAssetDatabase() : nullptr;
            const std::string meshAssetPath = assetDatabase != nullptr ? ResolveAssetPathFromID(*assetDatabase, mesh.GetMeshAssetID()) : "";
            RenderAssetReferenceField("Mesh", "##MeshReferencePath", "##MeshReference", meshAssetPath);

            const std::string materialAssetPath = assetDatabase != nullptr ? ResolveAssetPathFromID(*assetDatabase, mesh.GetMaterialAssetID()) : "";
            RenderAssetReferenceField("Material", "##MaterialReferencePath", "##MaterialReference", materialAssetPath);

            std::array<float, 3> boundsCenter = ToFloat3(mesh.GetBoundsCenter());
            if (ImGui::DragFloat3("Bounds Center", boundsCenter.data(), BoundsDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                mesh.SetBoundsCenter(FromFloat3(boundsCenter));
            }

            std::array<float, 3> boundsExtents = ToFloat3(mesh.GetBoundsExtents());
            if (ImGui::DragFloat3("Bounds Extents", boundsExtents.data(), BoundsDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                mesh.SetBoundsExtents(FromFloat3(boundsExtents));
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderCameraComponent(CameraComponent& camera)
    {
        ImGui::PushID(&camera);
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderEnabledCheckbox(camera);

            bool primary = camera.IsPrimary();
            if (ImGui::Checkbox("Primary", &primary))
            {
                camera.SetPrimary(primary);
            }

            int projectionMode = camera.GetProjectionMode() == CameraComponent::ProjectionMode::Perspective ? 0 : 1;
            const char* projectionModes[] = {"Perspective", "Orthographic"};
            if (ImGui::Combo("Projection", &projectionMode, projectionModes, IM_ARRAYSIZE(projectionModes)))
            {
                camera.SetProjectionMode(projectionMode == 0 ? CameraComponent::ProjectionMode::Perspective : CameraComponent::ProjectionMode::Orthographic);
            }

            float verticalFieldOfView = camera.GetVerticalFieldOfViewRadians();
            if (ImGui::DragFloat("FOV Radians", &verticalFieldOfView, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetVerticalFieldOfViewRadians(verticalFieldOfView);
            }

            float orthographicSize = camera.GetOrthographicSize();
            if (ImGui::DragFloat("Ortho Size", &orthographicSize, MediumDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetOrthographicSize(orthographicSize);
            }

            float aspectRatio = camera.GetAspectRatio();
            if (ImGui::DragFloat("Aspect", &aspectRatio, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetAspectRatio(aspectRatio);
            }

            float nearClipPlane = camera.GetNearClipPlane();
            if (ImGui::DragFloat("Near", &nearClipPlane, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetNearClipPlane(nearClipPlane);
            }

            float farClipPlane = camera.GetFarClipPlane();
            if (ImGui::DragFloat("Far", &farClipPlane, LargeDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetFarClipPlane(farClipPlane);
            }

            std::array<float, 4> clearColor = ToFloat4(camera.GetClearColor());
            if (ImGui::ColorEdit4("Clear Color", clearColor.data()))
            {
                camera.SetClearColor(ToRhiColor(clearColor));
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderLightComponent(LightComponent& light)
    {
        ImGui::PushID(&light);
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderEnabledCheckbox(light);

            int lightType = light.GetLightType() == LightType::Directional ? 0 : 1;
            const char* lightTypes[] = {"Directional", "Point"};
            if (ImGui::Combo("Type", &lightType, lightTypes, IM_ARRAYSIZE(lightTypes)))
            {
                light.SetLightType(lightType == 0 ? LightType::Directional : LightType::Point);
            }

            std::array<float, 3> color = ToFloat3(light.GetColor());
            if (ImGui::ColorEdit3("Color", color.data()))
            {
                light.SetColor(FromFloat3(color));
            }

            float intensity = light.GetIntensity();
            if (ImGui::DragFloat("Intensity", &intensity, MediumDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                light.SetIntensity(intensity);
            }

            float range = light.GetRange();
            if (ImGui::DragFloat("Range", &range, MediumDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                light.SetRange(range);
            }

            float innerConeAngle = light.GetInnerConeAngleRadians();
            if (ImGui::DragFloat("Inner Cone", &innerConeAngle, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                light.SetInnerConeAngleRadians(innerConeAngle);
            }

            float outerConeAngle = light.GetOuterConeAngleRadians();
            if (ImGui::DragFloat("Outer Cone", &outerConeAngle, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                light.SetOuterConeAngleRadians(outerConeAngle);
            }

            bool castShadows = light.CastShadows();
            if (ImGui::Checkbox("Cast Shadows", &castShadows))
            {
                light.SetCastShadows(castShadows);
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderColliderComponent(GameObject& gameObject, ColliderComponent& collider)
    {
        ImGui::PushID(&collider);
        const bool open = ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem("ColliderComponentContext"))
        {
            if (ImGui::MenuItem("Remove"))
            {
                const bool removed = gameObject.RemoveComponent<ColliderComponent>();
                (void)removed;
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            RenderEnabledCheckbox(collider);

            int shapeType = std::clamp(static_cast<int>(collider.GetShapeType()), 0, 2);
            const char* shapeTypes[] = {"Box", "Sphere", "Capsule"};
            if (ImGui::Combo("Shape", &shapeType, shapeTypes, IM_ARRAYSIZE(shapeTypes)))
            {
                collider.SetShapeType(static_cast<ColliderShapeType>(shapeType));
            }

            bool trigger = collider.IsTrigger();
            if (ImGui::Checkbox("Is Trigger", &trigger))
            {
                collider.SetTrigger(trigger);
            }

            std::array<float, 3> center = ToFloat3(collider.GetCenter());
            if (ImGui::DragFloat3("Center", center.data(), BoundsDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                collider.SetCenter(FromFloat3(center));
            }

            std::array<float, 3> size = ToFloat3(collider.GetSize());
            if (ImGui::DragFloat3("Size", size.data(), BoundsDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                collider.SetSize(FromFloat3(size));
            }

            float radius = collider.GetRadius();
            if (ImGui::DragFloat("Radius", &radius, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                collider.SetRadius(radius);
            }

            float height = collider.GetHeight();
            if (ImGui::DragFloat("Height", &height, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                collider.SetHeight(height);
            }

            int direction = std::clamp(static_cast<int>(collider.GetDirection()), 0, 2);
            const char* directions[] = {"X", "Y", "Z"};
            if (ImGui::Combo("Direction", &direction, directions, IM_ARRAYSIZE(directions)))
            {
                collider.SetDirection(static_cast<ColliderDirectionAxis>(direction));
            }

            float staticFriction = collider.GetStaticFriction();
            if (ImGui::DragFloat("Static Friction", &staticFriction, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                collider.SetStaticFriction(staticFriction);
            }

            float dynamicFriction = collider.GetDynamicFriction();
            if (ImGui::DragFloat("Dynamic Friction", &dynamicFriction, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                collider.SetDynamicFriction(dynamicFriction);
            }

            float bounciness = collider.GetBounciness();
            if (ImGui::DragFloat("Bounciness", &bounciness, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                collider.SetBounciness(bounciness);
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderRigidbodyComponent(GameObject& gameObject, RigidbodyComponent& rigidbody)
    {
        ImGui::PushID(&rigidbody);
        const bool open = ImGui::CollapsingHeader("Rigidbody", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem("RigidbodyComponentContext"))
        {
            if (ImGui::MenuItem("Remove"))
            {
                const bool removed = gameObject.RemoveComponent<RigidbodyComponent>();
                (void)removed;
                ImGui::EndPopup();
                ImGui::PopID();
                return;
            }
            ImGui::EndPopup();
        }

        if (open)
        {
            RenderEnabledCheckbox(rigidbody);

            float mass = rigidbody.GetMass();
            if (ImGui::DragFloat("Mass", &mass, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                rigidbody.SetMass(mass);
            }

            float linearDamping = rigidbody.GetLinearDamping();
            if (ImGui::DragFloat("Linear Damping", &linearDamping, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                rigidbody.SetLinearDamping(linearDamping);
            }

            float angularDamping = rigidbody.GetAngularDamping();
            if (ImGui::DragFloat("Angular Damping", &angularDamping, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                rigidbody.SetAngularDamping(angularDamping);
            }

            bool useGravity = rigidbody.UsesGravity();
            if (ImGui::Checkbox("Use Gravity", &useGravity))
            {
                rigidbody.SetUseGravity(useGravity);
            }

            bool kinematic = rigidbody.IsKinematic();
            if (ImGui::Checkbox("Is Kinematic", &kinematic))
            {
                rigidbody.SetKinematic(kinematic);
            }

            bool detectCollisions = rigidbody.DetectsCollisions();
            if (ImGui::Checkbox("Detect Collisions", &detectCollisions))
            {
                rigidbody.SetDetectCollisions(detectCollisions);
            }

            int interpolationMode = std::clamp(static_cast<int>(rigidbody.GetInterpolationMode()), 0, 2);
            const char* interpolationModes[] = {"None", "Interpolate", "Extrapolate"};
            if (ImGui::Combo("Interpolation", &interpolationMode, interpolationModes, IM_ARRAYSIZE(interpolationModes)))
            {
                rigidbody.SetInterpolationMode(static_cast<RigidbodyInterpolationMode>(interpolationMode));
            }

            int collisionDetectionMode = std::clamp(static_cast<int>(rigidbody.GetCollisionDetectionMode()), 0, 3);
            const char* collisionDetectionModes[] = {"Discrete", "Continuous", "Continuous Dynamic", "Continuous Speculative"};
            if (ImGui::Combo("Collision Detection", &collisionDetectionMode, collisionDetectionModes, IM_ARRAYSIZE(collisionDetectionModes)))
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
    }

    bool InspectorPanel::RenderScriptComponent(GameObject& gameObject, DotnetScriptableComponent& script)
    {
        ImGui::PushID(&script);
        const bool open = ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::BeginPopupContextItem("ScriptComponentContext"))
        {
            if (ImGui::MenuItem("Remove"))
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

    void InspectorPanel::RenderAddComponent(GameObject& gameObject)
    {
        if (editor_ == nullptr)
        {
            return;
        }

        if (!ImGui::CollapsingHeader("Add Component"))
        {
            return;
        }

        if (gameObject.GetComponent<ColliderComponent>() == nullptr && ImGui::Button("Add Collider"))
        {
            Result<ColliderComponent*> result = gameObject.AddComponentWithoutRenderRegistration<ColliderComponent>();
            if (!result)
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to add collider component: {}", result.GetError().GetMessage());
            }
        }

        if (gameObject.GetComponent<RigidbodyComponent>() == nullptr && ImGui::Button("Add Rigidbody"))
        {
            Result<RigidbodyComponent*> result = gameObject.AddComponentWithoutRenderRegistration<RigidbodyComponent>();
            if (!result)
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to add rigidbody component: {}", result.GetError().GetMessage());
            }
        }

        const std::vector<ScriptTypeInfo>& scriptTypes = editor_->GetScriptDatabase().GetScriptTypes();
        if (!scriptTypes.empty() && ImGui::BeginCombo("Script", "Select script"))
        {
            for (const ScriptTypeInfo& scriptType : scriptTypes)
            {
                const char* label = scriptType.displayName.empty() ? scriptType.typeName.c_str() : scriptType.displayName.c_str();
                if (ImGui::Selectable(label))
                {
                    ve::ScriptingSystem& scriptingSystem = editor_->GetRuntime().GetScriptingSystem();
                    Result<DotnetScriptableComponent*> result =
                        gameObject.AddComponentWithoutRenderRegistration<DotnetScriptableComponent>(scriptType.typeName, scriptingSystem);
                    if (!result)
                    {
                        VE_LOG_WARN_CATEGORY("Editor", "Failed to add script component '{}': {}", scriptType.typeName, result.GetError().GetMessage());
                    }
                    else
                    {
                        const ErrorCode ensureResult = result.GetValue()->EnsureScriptInstance(false);
                        if (ensureResult != ErrorCode::None)
                        {
                            VE_LOG_WARN_CATEGORY("Editor", "Failed to create editor script instance '{}': {}", scriptType.typeName, ToString(ensureResult));
                        }
                    }
                }
            }
            ImGui::EndCombo();
        }
    }

    void InspectorPanel::RenderAsset(const EditorAssetRecord& asset)
    {
        ImGui::TextUnformatted(asset.path.GetFilename().c_str());
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Asset", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Type: %s", EditorAssetDatabase::ToString(asset.type));
            ImGui::TextWrapped("AssetID: %s", asset.asset.id.ToString().c_str());
            ImGui::TextWrapped("Path: %s", asset.path.GetString().c_str());
            ImGui::TextWrapped("Meta: %s", asset.metaPath.GetString().c_str());
            ImGui::TextWrapped("Physical Path: %s", (editor_->GetAssetDatabase().GetProjectRoot() / asset.path).GetString().c_str());
            ImGui::Text("Imported: %s", asset.imported ? "Yes" : "No");

            if (!asset.importedPath.IsEmpty())
            {
                ImGui::TextWrapped("Imported Path: %s", asset.importedPath.GetString().c_str());
            }
        }

        if (asset.type == EditorAssetType::Material)
        {
            RenderMaterialAsset(asset);
        }
    }

    void InspectorPanel::RenderMaterialAsset(const EditorAssetRecord& asset)
    {
        if (editor_ == nullptr)
        {
            return;
        }

        if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        if (editor_->IsPlaying())
        {
            ImGui::TextDisabled("Stop Play mode to edit material assets.");
            return;
        }

        const EditorAssetDatabase& assetDatabase = editor_->GetAssetDatabase();
        const Path materialPhysicalPath = assetDatabase.GetProjectRoot() / asset.path;
        Result<boost::json::object> materialJson = ReadJsonObjectFile(materialPhysicalPath);
        if (!materialJson)
        {
            ImGui::TextDisabled("Failed to read material.");
            return;
        }

        const Path shaderProjectPath = ResolveProjectRelativeReference(ReadString(materialJson.GetValue(), "shader"), ".veshader");
        const EditorAssetRecord* shaderAsset = assetDatabase.FindAsset(shaderProjectPath);
        if (shaderAsset == nullptr)
        {
            ImGui::TextDisabled("Material shader was not found.");
            return;
        }

        Result<boost::json::object> shaderJson = ReadJsonObjectFile(assetDatabase.GetProjectRoot() / shaderAsset->asset.runtimePath);
        if (!shaderJson)
        {
            ImGui::TextDisabled("Failed to read imported shader.");
            return;
        }

        Result<ShaderMaterialLayout> layout = ReadShaderMaterialLayoutJson(shaderJson.GetValue());
        if (!layout)
        {
            ImGui::TextDisabled("Shader material layout is invalid.");
            return;
        }

        const boost::json::value* propertiesJson = materialJson.GetValue().if_contains("properties");
        if (propertiesJson == nullptr || !propertiesJson->is_object())
        {
            ImGui::TextDisabled("Material properties are missing.");
            return;
        }

        Result<std::vector<MaterialPropertyValue>> values = ResolveMaterialPropertyValues(layout.GetValue(), propertiesJson->as_object());
        if (!values)
        {
            ImGui::TextDisabled("Material properties are invalid.");
            return;
        }

        bool changed = false;
        bool textureChanged = false;
        std::vector<MaterialPropertyValue> editableValues = values.MoveValue();

        for (SizeT index = 0; index < layout.GetValue().properties.size() && index < editableValues.size(); ++index)
        {
            const ShaderMaterialPropertyDesc& property = layout.GetValue().properties[index];
            MaterialPropertyValue& value = editableValues[index];
            ImGui::PushID(property.name.c_str());

            if (value.type == MaterialPropertyType::Color)
            {
                std::array<float, 4> color = ToFloat4(value.vectorValue);
                if (ImGui::ColorEdit4(property.displayName.c_str(), color.data()))
                {
                    value.vectorValue = FromFloat4Vector(color);
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Float4)
            {
                std::array<float, 4> vector = ToFloat4(value.vectorValue);
                if (ImGui::DragFloat4(property.displayName.c_str(), vector.data(), FineDragSpeed, 0.0f, 0.0f, "%.3f"))
                {
                    value.vectorValue = FromFloat4Vector(vector);
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Float)
            {
                float floatValue = value.floatValue;
                if (ImGui::DragFloat(property.displayName.c_str(), &floatValue, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
                {
                    value.floatValue = floatValue;
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Texture2D)
            {
                ImGui::TextUnformatted(property.displayName.c_str());
                std::array<char, TextBufferSize> idBuffer = ToTextBuffer(value.assetValue.IsEmpty() ? "" : value.assetValue.ToString());
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##TextureAssetID", idBuffer.data(), idBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    Result<AssetID> parsedID = ParseAssetIDText(idBuffer.data());
                    if (parsedID)
                    {
                        value.assetValue = parsedID.GetValue();
                        changed = true;
                        textureChanged = true;
                    }
                }

                const std::string assetPath = ResolveAssetPathFromID(assetDatabase, value.assetValue);
                ImGui::TextDisabled("%s", assetPath.empty() ? "No texture" : assetPath.c_str());
                if (!value.assetValue.IsEmpty() && ImGui::Button("Clear"))
                {
                    value.assetValue = AssetID();
                    changed = true;
                    textureChanged = true;
                }
            }

            ImGui::PopID();
        }

        if (!changed)
        {
            return;
        }

        materialJson.GetValue()["properties"] = WriteMaterialPropertyValuesJson(layout.GetValue(), editableValues);
        const ErrorCode writeResult = FileSystem::WriteTextFile(materialPhysicalPath, JsonUtils::SerializePretty(materialJson.GetValue()));
        if (writeResult != ErrorCode::None)
        {
            ImGui::TextDisabled("Failed to save material.");
            return;
        }

        MaterialResource* loadedMaterial = FindLoadedMaterialResource(editor_->GetRuntime().GetResourceSystem(), asset.asset.id);
        if (loadedMaterial != nullptr)
        {
            for (SizeT index = 0; index < layout.GetValue().properties.size() && index < editableValues.size(); ++index)
            {
                const ErrorCode setResult = loadedMaterial->SetPropertyValue(layout.GetValue().properties[index].name, editableValues[index]);
                (void)setResult;
            }
            loadedMaterial->SyncRenderResource(editor_->GetRenderSystem());
        }

        if (textureChanged)
        {
            const ErrorCode refreshResult = editor_->GetAssetDatabase().Refresh();
            (void)refreshResult;
        }
    }
} // namespace ve::editor
