#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Tools/AssetTool/ObjImporter.h"

#include <imgui.h>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    namespace
    {
        struct AssetTreeNode
        {
            std::string name;
            std::string fullPath;
            std::vector<std::unique_ptr<AssetTreeNode>> children;
            std::vector<const AssetRecord*> assets;
        };

        [[nodiscard]] std::string MakeStatusError(std::string_view prefix, ErrorCode result)
        {
            std::string message(prefix);
            message += ": ";
            message += ToString(result);
            return message;
        }

        [[nodiscard]] bool IsNativeAuthoredAsset(AssetType type) noexcept
        {
            return type == AssetType::Scene || type == AssetType::Material;
        }

        [[nodiscard]] bool IsObjSource(const Path& path)
        {
            return path.GetExtension() == ".obj" || path.GetExtension() == ".OBJ";
        }

        [[nodiscard]] Path GetImportSourcePath(const AssetRecord& record)
        {
            return record.source.IsEmpty() ? record.path : record.source;
        }

        [[nodiscard]] Path GetDisplayPath(const AssetRecord& record)
        {
            return record.assetType == AssetType::SourceModel && !record.source.IsEmpty() ? record.source : record.path;
        }

        [[nodiscard]] std::vector<std::string> SplitPathSegments(const Path& path)
        {
            std::vector<std::string> segments;
            std::string segment;
            for (char value : path.GetString())
            {
                if (value == '/' || value == '\\')
                {
                    if (!segment.empty())
                    {
                        segments.push_back(std::move(segment));
                        segment.clear();
                    }
                    continue;
                }

                segment.push_back(value);
            }

            if (!segment.empty())
            {
                segments.push_back(std::move(segment));
            }

            return segments;
        }

        [[nodiscard]] bool CanImportAssetRecord(const AssetRecord& record)
        {
            return record.assetType == AssetType::SourceModel && IsObjSource(GetImportSourcePath(record));
        }

        [[nodiscard]] bool ValidateAssetRecord(const AssetDatabase& assetDatabase, const AssetRecord& record)
        {
            if (record.assetType == AssetType::SourceModel)
            {
                if (record.source.IsEmpty() || !FileSystem::IsFile(assetDatabase.ResolveProjectPath(record.source)))
                {
                    return false;
                }

                for (const AssetArtifact& artifact : record.artifacts)
                {
                    if (artifact.path.IsEmpty() || !FileSystem::IsFile(assetDatabase.ResolveProjectPath(artifact.path)))
                    {
                        return false;
                    }
                }

                return record.guid.IsValid() && !record.artifacts.empty();
            }

            return IsNativeAuthoredAsset(record.assetType) &&
                   FileSystem::IsFile(assetDatabase.ResolveProjectPath(record.path));
        }

        [[nodiscard]] AssetTreeNode& FindOrCreateChild(AssetTreeNode& parent, const std::string& name)
        {
            const auto iter = std::find_if(parent.children.begin(),
                                           parent.children.end(),
                                           [&name](const auto& child) { return child->name == name; });
            if (iter != parent.children.end())
            {
                return **iter;
            }

            auto child = std::make_unique<AssetTreeNode>();
            child->name = name;
            child->fullPath = parent.fullPath.empty() ? name : parent.fullPath + "/" + name;
            AssetTreeNode& result = *child;
            parent.children.push_back(std::move(child));
            return result;
        }

        void SortAssetTree(AssetTreeNode& node)
        {
            std::sort(node.children.begin(),
                      node.children.end(),
                      [](const auto& left, const auto& right) { return left->name < right->name; });
            std::sort(node.assets.begin(),
                      node.assets.end(),
                      [](const AssetRecord* left, const AssetRecord* right)
                      { return GetDisplayPath(*left).GetFilename() < GetDisplayPath(*right).GetFilename(); });

            for (const auto& child : node.children)
            {
                SortAssetTree(*child);
            }
        }

        [[nodiscard]] AssetTreeNode BuildAssetTree(const AssetDatabase& assetDatabase)
        {
            AssetTreeNode root;
            for (const AssetRecord& record : assetDatabase.GetRecords())
            {
                const std::vector<std::string> segments = SplitPathSegments(GetDisplayPath(record));
                if (segments.empty())
                {
                    root.assets.push_back(&record);
                    continue;
                }

                AssetTreeNode* parent = &root;
                for (SizeT index = 0; index + 1 < segments.size(); ++index)
                {
                    parent = &FindOrCreateChild(*parent, segments[index]);
                }

                parent->assets.push_back(&record);
            }

            SortAssetTree(root);
            return root;
        }
    } // namespace

    void WindowsEditorPanels::DrawAssetBrowser(EditorProjectService& projectService,
                                               EngineRuntime& runtime,
                                               std::string& statusMessage,
                                               const OpenSceneRequest& openSceneRequest)
    {
        ImGui::TextUnformatted("Asset Browser");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu asset(s)", projectService.GetAssetDatabase().GetRecords().size());
        ImGui::Separator();

        if (ImGui::BeginPopupContextWindow("AssetBrowserContextMenu",
                                           ImGuiPopupFlags_MouseButtonRight |
                                               ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Scan"))
            {
                const ErrorCode refreshResult = projectService.RefreshAssetDatabase();
                statusMessage = refreshResult == ErrorCode::None
                                    ? "AssetDatabase scan complete."
                                    : MakeStatusError("AssetDatabase scan failed", refreshResult);
            }

            if (ImGui::MenuItem("Validate"))
            {
                const ErrorCode validateResult = projectService.GetAssetDatabase().Validate();
                statusMessage = validateResult == ErrorCode::None
                                    ? "AssetDatabase validation passed."
                                    : MakeStatusError("AssetDatabase validation failed", validateResult);
            }

            ImGui::EndPopup();
        }

        DrawAssetTree(projectService, runtime, statusMessage, openSceneRequest);
    }

    void WindowsEditorPanels::DrawAssetTree(EditorProjectService& projectService,
                                            EngineRuntime& runtime,
                                            std::string& statusMessage,
                                            const OpenSceneRequest& openSceneRequest)
    {
        const AssetDatabase& assetDatabase = projectService.GetAssetDatabase();
        AssetTreeNode root = BuildAssetTree(assetDatabase);

        const auto drawAssetNode = [this, &projectService, &runtime, &statusMessage, &openSceneRequest](
                                       const AssetRecord& record)
        {
            const Path displayPath = GetDisplayPath(record);
            const std::string nodeId = displayPath.GetString();
            const std::string label = displayPath.GetFilename().empty() ? displayPath.GetString()
                                                                        : displayPath.GetFilename();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (IsAssetSelected(record))
            {
                flags |= ImGuiTreeNodeFlags_Selected;
            }

            ImGui::PushID(nodeId.c_str());
            ImGui::TreeNodeEx("Asset", flags, "%s", label.c_str());
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                SelectAssetRecord(record);
            }

            if (ImGui::BeginPopupContextItem())
            {
                SelectAssetRecord(record);
                DrawAssetCommands(projectService, runtime, statusMessage, record, openSceneRequest);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        };

        const auto drawDirectoryNode = [&drawAssetNode](const AssetTreeNode& node, const auto& drawSelf) -> void
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
            const bool opened = ImGui::TreeNodeEx(node.fullPath.c_str(), flags, "%s", node.name.c_str());
            if (!opened)
            {
                return;
            }

            for (const auto& child : node.children)
            {
                drawSelf(*child, drawSelf);
            }

            for (const AssetRecord* record : node.assets)
            {
                drawAssetNode(*record);
            }

            ImGui::TreePop();
        };

        for (const auto& child : root.children)
        {
            drawDirectoryNode(*child, drawDirectoryNode);
        }

        for (const AssetRecord* record : root.assets)
        {
            drawAssetNode(*record);
        }
    }

    void WindowsEditorPanels::DrawAssetCommands(EditorProjectService& projectService,
                                                EngineRuntime& runtime,
                                                std::string& statusMessage,
                                                const AssetRecord& record,
                                                const OpenSceneRequest& openSceneRequest)
    {
        (void)runtime;

        const AssetDatabase& assetDatabase = projectService.GetAssetDatabase();
        if (ImGui::MenuItem("Validate"))
        {
            const bool valid = ValidateAssetRecord(assetDatabase, record);
            statusMessage = valid ? "Asset validation passed: " + GetDisplayPath(record).GetString()
                                  : "Asset validation failed: " + GetDisplayPath(record).GetString();
        }

        if (record.assetType == AssetType::Scene)
        {
            if (ImGui::MenuItem("Open Scene") && openSceneRequest)
            {
                openSceneRequest(record.path);
            }

            return;
        }

        const bool canImport = CanImportAssetRecord(record);
        if (!canImport)
        {
            return;
        }

        if (!record.guid.IsValid())
        {
            if (ImGui::MenuItem("Import"))
            {
                ImportAsset(projectService, statusMessage, record, false);
            }

            return;
        }

        if (ImGui::MenuItem("Reimport"))
        {
            ImportAsset(projectService, statusMessage, record, true);
        }
    }

    void WindowsEditorPanels::ImportAsset(EditorProjectService& projectService,
                                          std::string& statusMessage,
                                          const AssetRecord& record,
                                          bool force)
    {
        AssetDatabase& assetDatabase = projectService.GetAssetDatabase();
        Result<ObjImportResult> importResult = ImportObjModel(assetDatabase, GetImportSourcePath(record), force);
        if (!importResult)
        {
            statusMessage = "Import failed: " + importResult.GetError().GetMessage();
            return;
        }

        (void)projectService.RefreshAssetDatabase();
        statusMessage = "Imported " + importResult.GetValue().metadataPath.GetString() + " (" +
                        std::to_string(importResult.GetValue().vertexCount) + " vertices).";
    }
} // namespace ve
