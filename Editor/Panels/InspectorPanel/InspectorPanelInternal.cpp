#include "Editor/Panels/InspectorPanel/InspectorPanelInternal.h"

#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scene/RigidbodyComponent.h"
#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"

#include <algorithm>
#include <cctype>

namespace ve::editor
{
    namespace
    {
        constexpr float InspectorLabelWidth = 128.0f;
    }
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

    [[nodiscard]] bool RenderFieldDragFloat(const char* label, float* value, float speed, float minValue, float maxValue)
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

    [[nodiscard]] bool RenderFieldInputText(const char* label, char* buffer, SizeT bufferSize, ImGuiInputTextFlags flags)
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
} // namespace ve::editor
