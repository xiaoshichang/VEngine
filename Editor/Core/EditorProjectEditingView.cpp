#include "Editor/Core/EditorProjectEditingView.h"

#include "Editor/Core/Editor.h"

#include <imgui.h>

#include <algorithm>

namespace ve::editor
{
    namespace
    {
        constexpr float PanelGap = 6.0F;
        constexpr float HierarchyWidth = 260.0F;
        constexpr float InspectorWidth = 310.0F;
        constexpr float AssetsHeight = 230.0F;
    } // namespace

    void ProjectEditingView::Init(Editor& editor)
    {
        if (initialized_)
        {
            return;
        }

        gameViewPanel_.Init(editor);
        initialized_ = true;
    }

    void ProjectEditingView::Render(Editor& editor)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        RenderMainMenu();
        projectDirectoryDialog_.Render(editor);

        const float menuBarHeight = ImGui::GetFrameHeight();
        const ImVec2 origin(viewport->WorkPos.x, viewport->WorkPos.y + menuBarHeight + PanelGap);
        const ImVec2 available(viewport->WorkSize.x, viewport->WorkSize.y - menuBarHeight - PanelGap);
        const float centerWidth =
            (std::max)(320.0F, available.x - HierarchyWidth - InspectorWidth - PanelGap * 2.0F);
        const float centerHeight = (std::max)(240.0F, available.y - AssetsHeight - PanelGap);
        const float splitCenterWidth = (std::max)(160.0F, (centerWidth - PanelGap) * 0.5F);

        hierarchyPanel_.Render(editor, origin, ImVec2(HierarchyWidth, available.y));

        const float centerX = origin.x + HierarchyWidth + PanelGap;
        sceneViewPanel_.Render(ImVec2(centerX, origin.y), ImVec2(splitCenterWidth, centerHeight));
        gameViewPanel_.Render(editor,
                              ImVec2(centerX + splitCenterWidth + PanelGap, origin.y),
                              ImVec2(splitCenterWidth, centerHeight));
        assetsPanel_.Render(
            editor, ImVec2(centerX, origin.y + centerHeight + PanelGap), ImVec2(centerWidth, AssetsHeight));

        inspectorPanel_.Render(
            editor, ImVec2(centerX + centerWidth + PanelGap, origin.y), ImVec2(InspectorWidth, available.y));
    }

    std::shared_ptr<RTRenderTexture> ProjectEditingView::GetGameViewTexture() const noexcept
    {
        return gameViewPanel_.GetGameViewTexture().GetRTRenderTexture();
    }

    void ProjectEditingView::RenderMainMenu()
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("Project"))
        {
            if (ImGui::MenuItem("Open Project..."))
            {
                projectDirectoryDialog_.RequestOpen();
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
} // namespace ve::editor
