#include "Editor/Core/EditorProjectEditingView.h"

#include "Editor/Core/Editor.h"

#include <algorithm>
#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        constexpr float PanelGap = 6.0F;
        constexpr float HierarchyWidth = 260.0F;
        constexpr float InspectorWidth = 310.0F;
        constexpr float AssetsHeight = 230.0F;
        constexpr float StatusBarHeight = 24.0F;
    } // namespace

    void ProjectEditingView::Init(Editor& editor)
    {
        if (initialized_)
        {
            return;
        }

        sceneViewPanel_.Init(editor);
        gameViewPanel_.Init(editor);
        initialized_ = true;
    }

    void ProjectEditingView::Render(Editor& editor)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        RenderMainMenu();
        projectDirectoryDialog_.Render(editor);
        buildPackageDialog_.Render(editor);

        const float menuBarHeight = ImGui::GetFrameHeight();
        const float contentTop = viewport->WorkPos.y + menuBarHeight + PanelGap;
        const float statusBarY = viewport->WorkPos.y + viewport->WorkSize.y - StatusBarHeight;
        const float contentHeight = (std::max)(0.0F, statusBarY - contentTop - PanelGap);
        const ImVec2 origin(viewport->WorkPos.x, contentTop);
        const ImVec2 available(viewport->WorkSize.x, contentHeight);
        const float centerWidth = (std::max)(320.0F, available.x - HierarchyWidth - InspectorWidth - PanelGap * 2.0F);
        const float centerHeight = (std::max)(240.0F, available.y - AssetsHeight - PanelGap);
        const float splitCenterWidth = (std::max)(160.0F, (centerWidth - PanelGap) * 0.5F);

        hierarchyPanel_.Render(editor, origin, ImVec2(HierarchyWidth, available.y));

        const float centerX = origin.x + HierarchyWidth + PanelGap;
        sceneViewPanel_.Render(editor, ImVec2(centerX, origin.y), ImVec2(splitCenterWidth, centerHeight));
        gameViewPanel_.Render(editor, ImVec2(centerX + splitCenterWidth + PanelGap, origin.y), ImVec2(splitCenterWidth, centerHeight));
        assetsPanel_.Render(editor, ImVec2(centerX, origin.y + centerHeight + PanelGap), ImVec2(centerWidth, AssetsHeight));

        inspectorPanel_.Render(editor, ImVec2(centerX + centerWidth + PanelGap, origin.y), ImVec2(InspectorWidth, available.y));

        RenderStatusBar(editor, ImVec2(viewport->WorkPos.x, statusBarY), ImVec2(viewport->WorkSize.x, StatusBarHeight));
    }

    std::shared_ptr<RTRenderTexture> ProjectEditingView::GetSceneViewTexture() const noexcept
    {
        return sceneViewPanel_.GetSceneViewTexture().GetRTRenderTexture();
    }

    RTCameraDesc ProjectEditingView::GetSceneViewCameraDesc() const noexcept
    {
        return sceneViewPanel_.GetSceneViewCameraDesc();
    }

    rhi::RhiFillMode ProjectEditingView::GetSceneViewFillMode() const noexcept
    {
        return sceneViewPanel_.GetFillMode();
    }

    bool ProjectEditingView::IsSceneViewGridEnabled() const noexcept
    {
        return sceneViewPanel_.IsGridEnabled();
    }

    Float32 ProjectEditingView::GetSceneViewGridOpacity() const noexcept
    {
        return sceneViewPanel_.GetGridOpacity();
    }

    Float32 ProjectEditingView::GetSceneViewGridUnitSize() const noexcept
    {
        return sceneViewPanel_.GetGridUnitSize();
    }

    const Gizmos& ProjectEditingView::GetSceneViewGizmos() const noexcept
    {
        return sceneViewPanel_.GetGizmos();
    }

    Matrix44 ProjectEditingView::GetSceneViewCameraLocalToWorld() const noexcept
    {
        return sceneViewPanel_.GetSceneViewCameraLocalToWorld();
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

        if (ImGui::BeginMenu("Build"))
        {
            if (ImGui::MenuItem("Package Windows..."))
            {
                buildPackageDialog_.RequestOpen();
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    void ProjectEditingView::RenderStatusBar(Editor& editor, const ImVec2& position, const ImVec2& size)
    {
        constexpr ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(size);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 3.0F));
        if (!ImGui::Begin("EditorStatusBar", nullptr, WindowFlags))
        {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        const Float32 framesPerSecond = editor.GetRuntime().GetTimeSystem().GetAverageFrameRate();
        ImGui::Text("FPS: %.1f", framesPerSecond);
        ImGui::End();
        ImGui::PopStyleVar();
    }
} // namespace ve::editor
