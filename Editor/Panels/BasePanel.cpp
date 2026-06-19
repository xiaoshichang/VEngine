#include "Editor/Panels/BasePanel.h"

#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] ImGuiWindowFlags FixedPanelFlags() noexcept
        {
            return ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
        }
    } // namespace

    void BasePanel::Render(const ImVec2& position, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::Begin(GetName(), nullptr, FixedPanelFlags());
        RenderContent();
        ImGui::End();
    }
} // namespace ve::editor
