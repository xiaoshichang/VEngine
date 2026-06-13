#include "Editor/Panels/GameViewPanel.h"

#include <imgui.h>

namespace ve::editor
{
    const char* GameViewPanel::GetName() const noexcept
    {
        return "Game View";
    }

    void GameViewPanel::RenderContent()
    {
        ImGui::TextUnformatted("Game View");
        ImGui::Separator();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImGui::Button("Game render target placeholder", canvasSize);
    }
} // namespace ve::editor
