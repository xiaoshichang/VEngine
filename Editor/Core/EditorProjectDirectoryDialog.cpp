#include "Editor/Core/EditorProjectDirectoryDialog.h"

#include "Editor/Core/Editor.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        constexpr float ProjectBrowserPathWidth = 840.0F;
        constexpr float ProjectBrowserListWidth = 840.0F;
        constexpr float ProjectBrowserListHeight = 360.0F;

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

    void ProjectDirectoryDialog::RequestOpen()
    {
        isOpen_ = true;
        errorMessage_.clear();
        entries_.clear();
        currentDirectory_.clear();
        RefreshPathBuffer();

        std::error_code errorCode;
        const std::filesystem::path currentPath = std::filesystem::current_path(errorCode);
        if (errorCode || !NavigateToDirectory(currentPath))
        {
            currentDirectory_ = std::filesystem::path(".");
            (void)NavigateToDirectory(currentDirectory_);
        }
    }

    void ProjectDirectoryDialog::Render(Editor& editor)
    {
        if (!isOpen_)
        {
            return;
        }

        ImGui::OpenPopup("Open Project Directory");
        if (!ImGui::BeginPopupModal("Open Project Directory", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextUnformatted("Select a project directory.");

        if (!errorMessage_.empty())
        {
            ImGui::TextWrapped("%s", errorMessage_.c_str());
        }

        ImGui::SetNextItemWidth(ProjectBrowserPathWidth);
        if (ImGui::InputText("##ProjectBrowserPath", pathBuffer_.data(), pathBuffer_.size(), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const std::filesystem::path inputPath = std::filesystem::path(TrimCopy(pathBuffer_.data()));
            if (!NavigateToDirectory(inputPath))
            {
                errorMessage_ = "The entered path is not a directory.";
            }
            else
            {
                errorMessage_.clear();
            }
        }

        if (ImGui::Button("Up", ImVec2(80.0F, 0.0F)))
        {
            const std::filesystem::path parentPath = currentDirectory_.parent_path();
            if (parentPath.empty() || !NavigateToDirectory(parentPath))
            {
                errorMessage_ = "Already at the root directory.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh", ImVec2(100.0F, 0.0F)))
        {
            (void)NavigateToDirectory(currentDirectory_);
        }

        ImGui::BeginChild("ProjectBrowserList", ImVec2(ProjectBrowserListWidth, ProjectBrowserListHeight), true);
        for (const std::filesystem::path& entryPath : entries_)
        {
            const std::string entryName = PathToUtf8String(entryPath.filename());
            const std::string entryId = PathToUtf8String(entryPath);
            const std::string entryLabel = (entryName.empty() ? std::string("<unnamed>") : entryName) + "##" + (entryId.empty() ? std::string("unnamed") : entryId);

            if (ImGui::Selectable(entryLabel.c_str()))
            {
                (void)NavigateToDirectory(entryPath);
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Open This Folder", ImVec2(180.0F, 0.0F)))
        {
            editor.OpenProject(PathToUtf8String(currentDirectory_));
            isOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0F, 0.0F)))
        {
            isOpen_ = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void ProjectDirectoryDialog::RefreshPathBuffer()
    {
        const std::string pathText = PathToUtf8String(currentDirectory_);
        std::snprintf(pathBuffer_.data(), pathBuffer_.size(), "%s", pathText.c_str());
    }

    bool ProjectDirectoryDialog::NavigateToDirectory(const std::filesystem::path& directoryPath)
    {
        std::error_code errorCode;
        if (!std::filesystem::exists(directoryPath, errorCode) || errorCode || !std::filesystem::is_directory(directoryPath, errorCode) || errorCode)
        {
            return false;
        }

        currentDirectory_ = directoryPath;
        entries_.clear();
        RefreshPathBuffer();

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(currentDirectory_, errorCode))
        {
            if (errorCode)
            {
                break;
            }

            if (entry.is_directory(errorCode) && !errorCode)
            {
                entries_.push_back(entry.path());
            }
        }

        std::sort(entries_.begin(), entries_.end());
        return !errorCode;
    }

    std::string ProjectDirectoryDialog::PathToUtf8String(const std::filesystem::path& path)
    {
        return path.string();
    }
} // namespace ve::editor
