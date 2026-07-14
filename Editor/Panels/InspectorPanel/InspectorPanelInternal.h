#pragma once

#include "Editor/Core/EditorAssetDatabase.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Scene/GameObject.h"

#include <array>
#include <imgui.h>
#include <string>
#include <string_view>

namespace ve
{
    class Component;
    class DotnetScriptableComponent;
    class RigidbodyComponent;
} // namespace ve

namespace ve::editor
{
    constexpr SizeT TextBufferSize = 512;
    constexpr float TransformDragSpeed = 0.05f;
    constexpr float RotationDragSpeed = 0.01f;
    constexpr float BoundsDragSpeed = 0.05f;
    constexpr float FineDragSpeed = 0.01f;
    constexpr float MediumDragSpeed = 0.1f;
    constexpr float LargeDragSpeed = 1.0f;
    constexpr float AddComponentButtonWidth = 220.0f;

    [[nodiscard]] std::array<char, TextBufferSize> ToTextBuffer(const std::string& value);
    [[nodiscard]] std::array<float, 3> ToFloat3(const Vector3& value) noexcept;
    [[nodiscard]] Vector3 FromFloat3(const std::array<float, 3>& value) noexcept;
    [[nodiscard]] std::array<float, 4> ToFloat4(const Quaternion& value) noexcept;
    [[nodiscard]] std::array<float, 4> ToFloat4(const rhi::RhiColor& value) noexcept;
    [[nodiscard]] Quaternion FromFloat4(const std::array<float, 4>& value) noexcept;
    [[nodiscard]] rhi::RhiColor ToRhiColor(const std::array<float, 4>& value) noexcept;
    [[nodiscard]] std::array<float, 4> ToFloat4(const Vector4& value) noexcept;
    [[nodiscard]] Vector4 FromFloat4Vector(const std::array<float, 4>& value) noexcept;

    [[nodiscard]] bool RenderFieldCheckbox(const char* label, bool* value);
    [[nodiscard]] bool RenderFieldDragFloat(const char* label, float* value, float speed, float minValue = 0.0f, float maxValue = 0.0f);
    [[nodiscard]] bool RenderFieldDragInt(const char* label, int* value);
    [[nodiscard]] bool RenderFieldDragFloat3(const char* label, float* value, float speed);
    [[nodiscard]] bool RenderFieldDragFloat4(const char* label, float* value, float speed);
    [[nodiscard]] bool RenderFieldCombo(const char* label, int* selectedIndex, const char* const* values, int valueCount);
    [[nodiscard]] bool RenderFieldInputText(const char* label, char* buffer, SizeT bufferSize, ImGuiInputTextFlags flags = 0);
    [[nodiscard]] bool RenderFieldColorEdit3(const char* label, float* value);
    [[nodiscard]] bool RenderFieldColorEdit4(const char* label, float* value);
    [[nodiscard]] bool RenderComponentHeader(const char* label, const ImVec4& color);
    bool RenderEnabledCheckbox(Component& component);
    void RenderConstraintCheckbox(RigidbodyComponent& rigidbody, const char* label, RigidbodyConstraintFlags flag);
    [[nodiscard]] bool MatchesFilter(std::string_view value, std::string_view filter);
    [[nodiscard]] bool HasDotnetScriptType(const GameObject& gameObject, const std::string& scriptTypeName) noexcept;
    [[nodiscard]] std::string ResolveAssetPathFromID(const EditorAssetDatabase& assetDatabase, const AssetID& id);
    void RenderAssetReferenceField(const char* label, const char* pathInputId, const char* buttonId, const std::string& assetPath);

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
} // namespace ve::editor
