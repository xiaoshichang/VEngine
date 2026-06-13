#include "Editor/Panels/HierarchyPanel.h"

#include <imgui.h>

namespace ve::editor
{
    const char* HierarchyPanel::GetName() const noexcept
    {
        return "Hierarchy";
    }

    void HierarchyPanel::RenderContent()
    {
        ImGui::TextUnformatted("Scene");
        ImGui::Separator();
        ImGui::Selectable("Main Camera");
        ImGui::Selectable("Directional Light");
        ImGui::Selectable("GameObject");
    }
} // namespace ve::editor
