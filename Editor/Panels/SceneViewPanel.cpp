#include "Editor/Panels/SceneViewPanel.h"

#include <imgui.h>

namespace ve::editor
{
    const char* SceneViewPanel::GetName() const noexcept
    {
        return "Scene View";
    }

    void SceneViewPanel::RenderContent()
    {
        ImGui::TextUnformatted("Scene View");
        ImGui::Separator();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        ImGui::Button("Scene render target placeholder", canvasSize);
    }
} // namespace ve::editor
