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
#include <cctype>
#include <imgui.h>
#include <string>
#include <string_view>
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
        constexpr float InspectorLabelWidth = 128.0f;
        constexpr float AddComponentButtonWidth = 220.0f;

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

        void BeginField(const char* label)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(InspectorLabelWidth);
            ImGui::SetNextItemWidth(-1.0f);
        }

        [[nodiscard]] bool RenderFieldCheckbox(const char* label, bool* value)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::Checkbox("##Value", value);
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderFieldDragFloat(const char* label, float* value, float speed, float minValue = 0.0f, float maxValue = 0.0f)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::DragFloat("##Value", value, speed, minValue, maxValue, "%.3f");
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderFieldDragInt(const char* label, int* value)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::DragInt("##Value", value);
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderFieldDragFloat3(const char* label, float* value, float speed)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::DragFloat3("##Value", value, speed, 0.0f, 0.0f, "%.3f");
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderFieldDragFloat4(const char* label, float* value, float speed)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::DragFloat4("##Value", value, speed, 0.0f, 0.0f, "%.3f");
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderFieldCombo(const char* label, int* selectedIndex, const char* const* values, int valueCount)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::Combo("##Value", selectedIndex, values, valueCount);
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderFieldInputText(const char* label, char* buffer, SizeT bufferSize, ImGuiInputTextFlags flags = 0)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::InputText("##Value", buffer, bufferSize, flags);
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderFieldColorEdit3(const char* label, float* value)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::ColorEdit3("##Value", value);
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderFieldColorEdit4(const char* label, float* value)
        {
            ImGui::PushID(label);
            BeginField(label);
            const bool changed = ImGui::ColorEdit4("##Value", value);
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool RenderComponentHeader(const char* label, const ImVec4& color)
        {
            ImGui::PushStyleColor(ImGuiCol_Header, color);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                  ImVec4((std::min)(color.x + 0.08f, 1.0f), (std::min)(color.y + 0.08f, 1.0f), (std::min)(color.z + 0.08f, 1.0f), color.w));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                  ImVec4((std::min)(color.x + 0.14f, 1.0f), (std::min)(color.y + 0.14f, 1.0f), (std::min)(color.z + 0.14f, 1.0f), color.w));
            const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopStyleColor(3);
            return open;
        }

        bool RenderEnabledCheckbox(Component& component)
        {
            bool enabled = component.IsEnabled();
            if (RenderFieldCheckbox("Enabled", &enabled))
            {
                component.SetEnabled(enabled);
                return true;
            }

            return false;
        }

        void RenderConstraintCheckbox(RigidbodyComponent& rigidbody, const char* label, RigidbodyConstraintFlags flag)
        {
            bool enabled = rigidbody.HasConstraint(flag);
            if (RenderFieldCheckbox(label, &enabled))
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

        [[nodiscard]] std::string ToLowerAscii(std::string_view value)
        {
            std::string lowered;
            lowered.reserve(value.size());
            for (const char character : value)
            {
                lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
            return lowered;
        }

        [[nodiscard]] bool MatchesFilter(std::string_view value, std::string_view filter)
        {
            if (filter.empty())
            {
                return true;
            }

            return ToLowerAscii(value).find(ToLowerAscii(filter)) != std::string::npos;
        }

        [[nodiscard]] bool HasDotnetScriptType(const GameObject& gameObject, const std::string& scriptTypeName) noexcept
        {
            for (SizeT scriptIndex = 0; scriptIndex < gameObject.GetScriptableComponentCount(); ++scriptIndex)
            {
                const ScriptableComponent* script = gameObject.GetScriptableComponent(scriptIndex);
                const DotnetScriptableComponent* dotnetScript = dynamic_cast<const DotnetScriptableComponent*>(script);
                if (dotnetScript != nullptr && dotnetScript->GetScriptTypeName() == scriptTypeName)
                {
                    return true;
                }
            }

            return false;
        }

        template<typename TComponent>
        [[nodiscard]] bool RenderRemoveComponentContextMenu(GameObject& gameObject, const char* popupID)
        {
            if (ImGui::BeginPopupContextItem(popupID))
            {
                bool removed = false;
                if (ImGui::MenuItem("Remove Component"))
                {
                    removed = gameObject.RemoveComponent<TComponent>();
                }
                ImGui::EndPopup();
                return removed;
            }

            return false;
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

            BeginField(label);
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
        if (RenderFieldInputText("Name", nameBuffer.data(), nameBuffer.size()))
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
            if (RenderMeshRenderComponent(gameObject, *mesh))
            {
                return;
            }
        }

        if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr)
        {
            if (RenderCameraComponent(gameObject, *camera))
            {
                return;
            }
        }

        if (LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr)
        {
            if (RenderLightComponent(gameObject, *light))
            {
                return;
            }
        }

        if (ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>(); collider != nullptr)
        {
            if (RenderColliderComponent(gameObject, *collider))
            {
                return;
            }
        }

        if (RigidbodyComponent* rigidbody = gameObject.GetComponent<RigidbodyComponent>(); rigidbody != nullptr)
        {
            if (RenderRigidbodyComponent(gameObject, *rigidbody))
            {
                return;
            }
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

            bool primary = camera.IsPrimary();
            if (RenderFieldCheckbox("Primary", &primary))
            {
                camera.SetPrimary(primary);
            }

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
            if (RenderFieldDragFloat("Aspect", &aspectRatio, FineDragSpeed))
            {
                camera.SetAspectRatio(aspectRatio);
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

    void InspectorPanel::RenderAddComponent(GameObject& gameObject)
    {
        if (editor_ == nullptr)
        {
            return;
        }

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float buttonWidth = (std::min)(AddComponentButtonWidth, availableWidth);
        const float buttonOffset = (std::max)(0.0f, (availableWidth - buttonWidth) * 0.5f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonOffset);
        if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0.0f)))
        {
            addComponentFilter_[0] = '\0';
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (!ImGui::BeginPopup("AddComponentPopup"))
        {
            return;
        }

        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("Filter", addComponentFilter_, sizeof(addComponentFilter_));
        ImGui::Separator();

        const std::string_view filter(addComponentFilter_);
        bool renderedAnyComponent = false;

        auto renderAddEntry = [&](const char* label, auto addComponent)
        {
            if (!MatchesFilter(label, filter))
            {
                return;
            }

            renderedAnyComponent = true;
            if (ImGui::Selectable(label))
            {
                addComponent();
                ImGui::CloseCurrentPopup();
            }
        };

        if (gameObject.GetComponent<MeshRenderComponent>() == nullptr)
        {
            renderAddEntry("Mesh Renderer",
                           [&gameObject]()
                           {
                               Result<MeshRenderComponent*> result = gameObject.AddComponent<MeshRenderComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add mesh renderer component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        if (gameObject.GetComponent<CameraComponent>() == nullptr)
        {
            renderAddEntry("Camera",
                           [&gameObject]()
                           {
                               Result<CameraComponent*> result = gameObject.AddComponent<CameraComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add camera component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        if (gameObject.GetComponent<LightComponent>() == nullptr)
        {
            renderAddEntry("Light",
                           [&gameObject]()
                           {
                               Result<LightComponent*> result = gameObject.AddComponent<LightComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add light component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        if (gameObject.GetComponent<ColliderComponent>() == nullptr)
        {
            renderAddEntry("Collider",
                           [&gameObject]()
                           {
                               Result<ColliderComponent*> result = gameObject.AddComponent<ColliderComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add collider component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        if (gameObject.GetComponent<RigidbodyComponent>() == nullptr)
        {
            renderAddEntry("Rigidbody",
                           [&gameObject]()
                           {
                               Result<RigidbodyComponent*> result = gameObject.AddComponent<RigidbodyComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add rigidbody component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        const std::vector<ScriptTypeInfo>& scriptTypes = editor_->GetScriptDatabase().GetScriptTypes();
        for (const ScriptTypeInfo& scriptType : scriptTypes)
        {
            if (HasDotnetScriptType(gameObject, scriptType.typeName))
            {
                continue;
            }

            const char* label = scriptType.displayName.empty() ? scriptType.typeName.c_str() : scriptType.displayName.c_str();
            if (!MatchesFilter(label, filter) && !MatchesFilter(scriptType.typeName, filter))
            {
                continue;
            }

            renderedAnyComponent = true;
            if (ImGui::Selectable(label))
            {
                ScriptingSystem& scriptingSystem = editor_->GetRuntime().GetScriptingSystem();
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
                ImGui::CloseCurrentPopup();
            }
        }

        if (!renderedAnyComponent)
        {
            ImGui::TextDisabled("No components available.");
        }

        ImGui::EndPopup();
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
                if (RenderFieldColorEdit4(property.displayName.c_str(), color.data()))
                {
                    value.vectorValue = FromFloat4Vector(color);
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Float4)
            {
                std::array<float, 4> vector = ToFloat4(value.vectorValue);
                if (RenderFieldDragFloat4(property.displayName.c_str(), vector.data(), FineDragSpeed))
                {
                    value.vectorValue = FromFloat4Vector(vector);
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Float)
            {
                float floatValue = value.floatValue;
                if (RenderFieldDragFloat(property.displayName.c_str(), &floatValue, FineDragSpeed))
                {
                    value.floatValue = floatValue;
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Texture2D)
            {
                std::array<char, TextBufferSize> idBuffer = ToTextBuffer(value.assetValue.IsEmpty() ? "" : value.assetValue.ToString());
                if (RenderFieldInputText(property.displayName.c_str(), idBuffer.data(), idBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
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
