#include "Editor/Core/EditorProjectSelectionView.h"

#include "Editor/Core/Editor.h"

#include <imgui.h>

#include <algorithm>

namespace ve::editor
{
    namespace
    {
        constexpr float ProjectSelectionPanelWidth = 760.0F;

        [[nodiscard]] std::string TrimCopy(std::string text)
        {
            const size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return {};
            }

            const size_t last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1);
        }
    } // namespace

    void ProjectSelectionView::Render(Editor& editor)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
        ImGui::Begin("Project Selection", nullptr, windowFlags);

        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const float panelWidth = (std::min)(ProjectSelectionPanelWidth, contentWidth);
        ImGui::SetCursorPosX((contentWidth - panelWidth) * 0.5F);

        ImGui::BeginChild("ProjectSelectionContent", ImVec2(panelWidth, 0.0F), false);
        ImGui::Spacing();
        ImGui::TextUnformatted("VEngine Editor");
        ImGui::Separator();

        ImGui::TextUnformatted("New Project");
        ImGui::SetNextItemWidth(-120.0F);
        ImGui::InputText("##NewProjectPath", newProjectPathBuffer_.data(), newProjectPathBuffer_.size());
        ImGui::SameLine();
        if (ImGui::Button("Create", ImVec2(104.0F, 0.0F)))
        {
            const std::string path = TrimCopy(newProjectPathBuffer_.data());
            if (!path.empty())
            {
                editor.OpenProject(path);
            }
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Open Project");
        if (ImGui::Button("Open...", ImVec2(160.0F, 0.0F)))
        {
            projectDirectoryDialog_.RequestOpen();
        }

        projectDirectoryDialog_.Render(editor);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted("Recent Projects");

        std::string selectedRecentProjectPath;
        const std::vector<std::string>& recentProjects = editor.GetRecentProjects();
        if (recentProjects.empty())
        {
            ImGui::TextDisabled("No recent projects.");
        }
        else
        {
            ImGui::BeginChild("RecentProjectList", ImVec2(0.0F, 0.0F), true);
            for (const std::string& projectPath : recentProjects)
            {
                const std::string displayName = Editor::GetProjectDisplayName(projectPath);
                ImGui::PushID(projectPath.c_str());
                if (ImGui::Selectable(displayName.c_str(), false))
                {
                    selectedRecentProjectPath = projectPath;
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", projectPath.c_str());
                }
                ImGui::TextDisabled("%s", projectPath.c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        ImGui::EndChild();
        ImGui::End();

        if (!selectedRecentProjectPath.empty())
        {
            editor.OpenProject(std::move(selectedRecentProjectPath));
        }
    }
} // namespace ve::editor
