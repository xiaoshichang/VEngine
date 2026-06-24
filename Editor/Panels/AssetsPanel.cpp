#include "Editor/Panels/AssetsPanel.h"

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorAssetDatabase.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <imgui.h>
#include <set>
#include <string>
#include <string_view>

namespace ve::editor
{
    namespace
    {
        constexpr float DirectoryTreeWidth = 220.0F;

        [[nodiscard]] const char* GetAssetTypeLabel(EditorAssetType type) noexcept
        {
            return EditorAssetDatabase::ToString(type);
        }

        [[nodiscard]] bool IsSameDirectory(const Path& left, const Path& right) noexcept
        {
            return left.GetString() == right.GetString();
        }

        [[nodiscard]] bool IsDirectChildAsset(const EditorAssetRecord& asset, const Path& directory)
        {
            return asset.path.GetParentPath() == directory;
        }

        [[nodiscard]] bool IsChildDirectory(const Path& child, const Path& parent)
        {
            const Path childParent = child.GetParentPath();
            return childParent == parent;
        }

        [[nodiscard]] std::string GetDisplayName(const Path& path)
        {
            const std::string filename = path.GetFilename();
            return filename.empty() ? path.GetString() : filename;
        }

        [[nodiscard]] bool HasChildDirectory(const std::vector<Path>& directories, const Path& directory)
        {
            return std::any_of(directories.begin(), directories.end(), [&directory](const Path& candidate) { return IsChildDirectory(candidate, directory); });
        }

        [[nodiscard]] bool DirectoryContainsAsset(const EditorAssetDatabase& assetDatabase, const Path& directory)
        {
            const auto& assetIDsByAssetPath = assetDatabase.GetAssetIDsByAssetPath();
            return std::any_of(assetIDsByAssetPath.begin(),
                               assetIDsByAssetPath.end(),
                               [&assetDatabase, &directory](const auto& pair)
                               {
                                   const EditorAssetRecord* asset = assetDatabase.FindAssetByID(pair.second);
                                   return asset != nullptr && IsDirectChildAsset(*asset, directory);
                               });
        }
    } // namespace

    void AssetsPanel::Init(Editor& editor)
    {
        editor_ = &editor;
    }

    const char* AssetsPanel::GetName() const noexcept
    {
        return "Assets";
    }

    void AssetsPanel::RenderContent()
    {
        if (editor_ == nullptr)
        {
            return;
        }

        EditorAssetDatabase& assetDatabase = editor_->GetAssetDatabase();
        if (!assetDatabase.IsInitialized())
        {
            ImGui::TextDisabled("No project is open.");
            return;
        }

        std::vector<Path> directories = BuildDirectoryList(assetDatabase);
        if (std::none_of(directories.begin(), directories.end(), [this](const Path& directory) { return IsSameDirectory(directory, currentDirectory_); }))
        {
            currentDirectory_ = Path("Assets");
        }

        if (ImGui::BeginChild("AssetDirectories", ImVec2(DirectoryTreeWidth, 0.0F), ImGuiChildFlags_ResizeX))
        {
            RenderDirectoryTree(directories, Path("Assets"));
            if (ImGui::BeginPopupContextWindow("AssetDirectoryContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("Reimport All"))
                {
                    (void)assetDatabase.ReimportAll();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        if (ImGui::BeginChild("AssetFiles", ImVec2(0.0F, 0.0F)))
        {
            RenderAssetFiles(assetDatabase);
            if (ImGui::BeginPopupContextWindow("AssetFilesContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("Reimport All"))
                {
                    (void)assetDatabase.ReimportAll();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::EndChild();
    }

    void AssetsPanel::RenderDirectoryTree(const std::vector<Path>& directories, const Path& directory)
    {
        const bool hasChildren = HasChildDirectory(directories, directory);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (directory == Path("Assets"))
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }

        if (directory == currentDirectory_)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        if (!hasChildren)
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const std::string displayName = GetDisplayName(directory);
        const bool open = ImGui::TreeNodeEx(directory.GetString().c_str(), flags, "%s", displayName.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            currentDirectory_ = directory;
            editor_->ClearSelection();
        }

        if (!hasChildren)
        {
            return;
        }

        if (open)
        {
            for (const Path& childDirectory : directories)
            {
                if (IsChildDirectory(childDirectory, directory))
                {
                    RenderDirectoryTree(directories, childDirectory);
                }
            }

            ImGui::TreePop();
        }
    }

    void AssetsPanel::RenderAssetFiles(EditorAssetDatabase& assetDatabase)
    {
        ImGui::TextUnformatted(currentDirectory_.GetString().c_str());
        ImGui::Separator();

        if (!DirectoryContainsAsset(assetDatabase, currentDirectory_))
        {
            ImGui::TextDisabled("Empty folder.");
            return;
        }

        if (ImGui::BeginTable("AssetFilesTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 92.0F);
            ImGui::TableSetupColumn("Imported", ImGuiTableColumnFlags_WidthFixed, 76.0F);
            ImGui::TableHeadersRow();

            std::vector<const EditorAssetRecord*> assetsInDirectory;
            assetsInDirectory.reserve(assetDatabase.GetAssetCount());
            for (const auto& pair : assetDatabase.GetAssetIDsByAssetPath())
            {
                const EditorAssetRecord* asset = assetDatabase.FindAssetByID(pair.second);
                if (asset != nullptr && IsDirectChildAsset(*asset, currentDirectory_))
                {
                    assetsInDirectory.push_back(asset);
                }
            }

            std::sort(assetsInDirectory.begin(),
                      assetsInDirectory.end(),
                      [](const EditorAssetRecord* left, const EditorAssetRecord* right) { return left->path.GetString() < right->path.GetString(); });

            for (const EditorAssetRecord* asset : assetsInDirectory)
            {
                const bool selected = editor_->GetSelectionType() == EditorSelectionType::Asset && editor_->GetSelectedAssetPath() == asset->path;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string filename = asset->path.GetFilename();
                const ImGuiSelectableFlags selectableFlags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap;
                if (ImGui::Selectable(filename.c_str(), selected, selectableFlags))
                {
                    editor_->SetSelectedAsset(asset->path);
                }

                if (ImGui::BeginPopupContextItem())
                {
                    if (ImGui::MenuItem("Reimport"))
                    {
                        editor_->SetSelectedAsset(asset->path);
                        (void)assetDatabase.ReimportAsset(asset->path);
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(GetAssetTypeLabel(asset->type));

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(asset->imported ? "Yes" : "No");
            }

            ImGui::EndTable();
        }
    }

    std::vector<Path> AssetsPanel::BuildDirectoryList(const EditorAssetDatabase& assetDatabase) const
    {
        std::set<std::string> directorySet{"Assets"};
        std::vector<Path> pendingDirectories{Path("Assets")};

        while (!pendingDirectories.empty())
        {
            const Path directory = pendingDirectories.back();
            pendingDirectories.pop_back();

            Result<std::vector<FileSystem::DirectoryEntry>> entries = FileSystem::ListDirectory(assetDatabase.GetProjectRoot() / directory);
            if (!entries)
            {
                continue;
            }

            for (const FileSystem::DirectoryEntry& entry : entries.GetValue())
            {
                if (entry.type != FileSystem::DirectoryEntryType::Directory)
                {
                    continue;
                }

                const Path childDirectory = directory / entry.name;
                if (directorySet.insert(childDirectory.GetString()).second)
                {
                    pendingDirectories.push_back(childDirectory);
                }
            }
        }

        std::vector<Path> directories;
        directories.reserve(directorySet.size());
        for (const std::string& directory : directorySet)
        {
            directories.emplace_back(directory);
        }
        std::sort(directories.begin(), directories.end(), [](const Path& left, const Path& right) { return left.GetString() < right.GetString(); });
        return directories;
    }
} // namespace ve::editor
