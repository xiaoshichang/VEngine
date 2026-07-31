#include "Editor/Panels/ProfilePanel/ProfilePanel.h"

#include <imgui.h>

namespace ve::editor
{
    const char* ProfilePanel::GetName() const noexcept
    {
        return "Profile";
    }

    void ProfilePanel::RenderContent()
    {
        ImGui::TextDisabled("Profile panel is not implemented yet.");
    }
} // namespace ve::editor
