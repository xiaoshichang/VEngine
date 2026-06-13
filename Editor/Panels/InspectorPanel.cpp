#include "Editor/Panels/InspectorPanel.h"

#include <array>
#include <imgui.h>

namespace ve::editor
{
    const char* InspectorPanel::GetName() const noexcept
    {
        return "Inspector";
    }

    void InspectorPanel::RenderContent()
    {
        ImGui::TextUnformatted("Inspector");
        ImGui::Separator();
        ImGui::TextDisabled("No object selected.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Transform");

        std::array<float, 3> positionValues{0.0F, 0.0F, 0.0F};
        std::array<float, 3> rotationValues{0.0F, 0.0F, 0.0F};
        std::array<float, 3> scaleValues{1.0F, 1.0F, 1.0F};
        ImGui::InputFloat3("Position", positionValues.data(), "%.2f", ImGuiInputTextFlags_ReadOnly);
        ImGui::InputFloat3("Rotation", rotationValues.data(), "%.2f", ImGuiInputTextFlags_ReadOnly);
        ImGui::InputFloat3("Scale", scaleValues.data(), "%.2f", ImGuiInputTextFlags_ReadOnly);
    }
} // namespace ve::editor
