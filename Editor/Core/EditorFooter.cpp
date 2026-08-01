#include "Editor/Core/EditorFooter.h"

#include "Editor/Core/Editor.h"
#include "Editor/Panels/ConsolePanel/ConsolePanel.h"

#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        constexpr Float32 FooterBottomMargin = 4.0F;

        [[nodiscard]] Float32 GetFooterHeight() noexcept
        {
            return ImGui::GetFrameHeight() + FooterBottomMargin;
        }

        [[nodiscard]] Float32 GetFooterY() noexcept
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            return viewport->WorkPos.y + viewport->WorkSize.y - GetFooterHeight() - FooterBottomMargin;
        }
    } // namespace

    Float32 EditorFooter::GetContentBottom() const noexcept
    {
        return GetFooterY();
    }

    void EditorFooter::Render(Editor& editor)
    {
        const Float64 currentUiTime = ImGui::GetTime();
        if (lastRefreshTime_ < 0.0 || currentUiTime - lastRefreshTime_ >= 1.0)
        {
            frameRate_ = editor.GetRuntime().GetTimeSystem().GetAverageFrameRate();
            lastRefreshTime_ = currentUiTime;
        }
        lastLogLine_ = GetLastEditorLogLine();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr ImGuiWindowFlags FooterWindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, GetFooterY()), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, GetFooterHeight()), ImGuiCond_Always);
        if (ImGui::Begin("EditorFooter", nullptr, FooterWindowFlags))
        {
            if (ImGui::BeginTable("EditorFooterContent", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
            {
                ImGui::TableSetupColumn("FrameRate", ImGuiTableColumnFlags_WidthStretch, 0.1F);
                ImGui::TableSetupColumn("LastLog", ImGuiTableColumnFlags_WidthStretch, 0.9F);
                ImGui::TableNextColumn();
                ImGui::Text("FPS: %.1f", frameRate_);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(lastLogLine_.c_str());
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
} // namespace ve::editor
