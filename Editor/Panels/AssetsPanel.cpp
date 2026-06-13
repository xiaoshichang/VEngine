#include "Editor/Panels/AssetsPanel.h"

#include <imgui.h>

namespace ve::editor
{
    const char* AssetsPanel::GetName() const noexcept
    {
        return "Assets";
    }

    void AssetsPanel::RenderContent()
    {
        ImGui::TextUnformatted("Assets");
        ImGui::Separator();
        ImGui::Columns(2, "AssetColumns", true);
        ImGui::Selectable("Assets");
        ImGui::Selectable("Scenes");
        ImGui::Selectable("Materials");
        ImGui::Selectable("Textures");
        ImGui::NextColumn();
        ImGui::Selectable("SampleScene.vescene");
        ImGui::Selectable("Default.vematerial");
        ImGui::Selectable("PlaceholderTexture.veasset");
        ImGui::Columns(1);
    }
} // namespace ve::editor
