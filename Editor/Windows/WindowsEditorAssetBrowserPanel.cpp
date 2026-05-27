#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Tools/AssetTool/ObjImporter.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    namespace
    {
        enum class AssetArtifactStatus
        {
            Authored,
            Ready,
            Missing,
            Unknown,
        };

        [[nodiscard]] std::string MakeStatusError(std::string_view prefix, ErrorCode result)
        {
            std::string message(prefix);
            message += ": ";
            message += ToString(result);
            return message;
        }

        [[nodiscard]] const char* ToDisplayString(AssetArtifactStatus status) noexcept
        {
            switch (status)
            {
            case AssetArtifactStatus::Authored:
                return "Authored";
            case AssetArtifactStatus::Ready:
                return "Ready";
            case AssetArtifactStatus::Missing:
                return "Missing";
            case AssetArtifactStatus::Unknown:
            default:
                return "Unknown";
            }
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

        [[nodiscard]] bool CanImportAssetRecord(const AssetRecord& record)
        {
            return record.assetType == AssetType::SourceModel && IsObjSource(GetImportSourcePath(record));
        }

        [[nodiscard]] std::string FormatGuidForUi(const AssetGuid& guid)
        {
            return guid.IsValid() ? guid.ToString() : std::string("(unimported)");
        }

        [[nodiscard]] AssetArtifactStatus GetArtifactStatus(const AssetDatabase& assetDatabase,
                                                            const AssetRecord& record)
        {
            if (IsNativeAuthoredAsset(record.assetType))
            {
                return AssetArtifactStatus::Authored;
            }

            if (record.assetType != AssetType::SourceModel || !record.guid.IsValid() || record.artifacts.empty())
            {
                return AssetArtifactStatus::Unknown;
            }

            for (const AssetArtifact& artifact : record.artifacts)
            {
                if (artifact.path.IsEmpty() || !FileSystem::IsFile(assetDatabase.ResolveProjectPath(artifact.path)))
                {
                    return AssetArtifactStatus::Missing;
                }
            }

            return AssetArtifactStatus::Ready;
        }

        [[nodiscard]] std::string GetImporterName(const AssetDatabase& assetDatabase, const AssetRecord& record)
        {
            if (IsNativeAuthoredAsset(record.assetType))
            {
                return "Native";
            }

            if (!record.metadataPath.IsEmpty() &&
                FileSystem::IsFile(assetDatabase.ResolveProjectPath(record.metadataPath)))
            {
                Result<SourceAssetMetadata> metadata = assetDatabase.LoadSourceMetadata(record.metadataPath);
                if (metadata && !metadata.GetValue().importer.empty())
                {
                    return metadata.GetValue().importer;
                }
            }

            return CanImportAssetRecord(record) ? "ObjModel" : "Unknown";
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

        if (ImGui::Button("Scan"))
        {
            const ErrorCode refreshResult = projectService.RefreshAssetDatabase();
            statusMessage = refreshResult == ErrorCode::None
                                ? "AssetDatabase scan complete."
                                : MakeStatusError("AssetDatabase scan failed", refreshResult);
        }

        ImGui::SameLine();
        if (ImGui::Button("Validate"))
        {
            const ErrorCode validateResult = projectService.GetAssetDatabase().Validate();
            statusMessage = validateResult == ErrorCode::None
                                ? "AssetDatabase validation passed."
                                : MakeStatusError("AssetDatabase validation failed", validateResult);
        }

        ImGui::Separator();
        DrawAssetTable(projectService, runtime, statusMessage, openSceneRequest);
    }

    void WindowsEditorPanels::DrawAssetTable(EditorProjectService& projectService,
                                             EngineRuntime& runtime,
                                             std::string& statusMessage,
                                             const OpenSceneRequest& openSceneRequest)
    {
        const AssetDatabase& assetDatabase = projectService.GetAssetDatabase();
        std::vector<const AssetRecord*> records;
        records.reserve(assetDatabase.GetRecords().size());
        for (const AssetRecord& record : assetDatabase.GetRecords())
        {
            records.push_back(&record);
        }

        std::sort(records.begin(),
                  records.end(),
                  [](const AssetRecord* left, const AssetRecord* right)
                  { return left->path.GetString() < right->path.GetString(); });

        constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
                                               ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                               ImGuiTableFlags_SizingStretchProp;
        if (!ImGui::BeginTable("AssetBrowserTable", 6, tableFlags))
        {
            return;
        }

        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.36f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("Importer", ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableSetupColumn("Artifact", ImGuiTableColumnFlags_WidthStretch, 0.10f);
        ImGui::TableSetupColumn("Commands", ImGuiTableColumnFlags_WidthStretch, 0.10f);
        ImGui::TableHeadersRow();

        for (const AssetRecord* record : records)
        {
            ImGui::PushID(record);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(record->path.GetString().c_str());
            if (!record->source.IsEmpty() && record->source != record->path)
            {
                ImGui::TextDisabled("%s", record->source.GetString().c_str());
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(ToString(record->assetType));

            ImGui::TableSetColumnIndex(2);
            const std::string guidText = FormatGuidForUi(record->guid);
            ImGui::TextUnformatted(guidText.c_str());

            ImGui::TableSetColumnIndex(3);
            const std::string importer = GetImporterName(assetDatabase, *record);
            ImGui::TextUnformatted(importer.c_str());

            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(ToDisplayString(GetArtifactStatus(assetDatabase, *record)));

            ImGui::TableSetColumnIndex(5);
            DrawAssetCommands(projectService, runtime, statusMessage, *record, openSceneRequest);

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    void WindowsEditorPanels::DrawAssetCommands(EditorProjectService& projectService,
                                                EngineRuntime& runtime,
                                                std::string& statusMessage,
                                                const AssetRecord& record,
                                                const OpenSceneRequest& openSceneRequest)
    {
        (void)runtime;

        if (record.assetType == AssetType::Scene)
        {
            if (ImGui::SmallButton("Open") && openSceneRequest)
            {
                openSceneRequest(record.path);
            }

            return;
        }

        const bool canImport = CanImportAssetRecord(record);
        if (!canImport)
        {
            ImGui::TextDisabled("-");
            return;
        }

        if (!record.guid.IsValid())
        {
            if (ImGui::SmallButton("Import"))
            {
                ImportAsset(projectService, statusMessage, record, false);
            }

            return;
        }

        if (ImGui::SmallButton("Reimport"))
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
