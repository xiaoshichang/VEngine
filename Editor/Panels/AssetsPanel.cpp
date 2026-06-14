#include "Editor/Panels/AssetsPanel.h"

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorAssetDatabase.h"

#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] const char* GetAssetTypeLabel(EditorAssetType type) noexcept
        {
            return EditorAssetDatabase::ToString(type);
        }
    } // namespace

    void AssetsPanel::Render(Editor& editor, const ImVec2& position, const ImVec2& size)
    {
        editor_ = &editor;
        BasePanel::Render(position, size);
        editor_ = nullptr;
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

        ImGui::Text("Assets (%zu)", assetDatabase.GetAssetCount());
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            (void)assetDatabase.Refresh();
        }
        ImGui::Separator();

        if (!assetDatabase.IsInitialized())
        {
            ImGui::TextDisabled("No project is open.");
            return;
        }

        if (ImGui::BeginTable("AssetDatabaseTable", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 92.0F);
            ImGui::TableSetupColumn("Path");
            ImGui::TableSetupColumn("Imported", ImGuiTableColumnFlags_WidthFixed, 76.0F);
            ImGui::TableSetupColumn("Source");
            ImGui::TableHeadersRow();

            for (const EditorAssetRecord& asset : assetDatabase.GetAssets())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(GetAssetTypeLabel(asset.type));

                ImGui::TableSetColumnIndex(1);
                ImGui::Selectable(asset.path.GetString().c_str(), false, ImGuiSelectableFlags_SpanAllColumns);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(asset.imported ? "Yes" : "No");

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(asset.sourcePath.GetString().c_str());
            }

            ImGui::EndTable();
        }
    }
} // namespace ve::editor
