#include "Editor/Core/EditorProjectEditingView.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Platform.h"

#include <algorithm>
#include <imgui.h>
#include <string>
#include <vector>

namespace ve::editor
{
    namespace
    {
        constexpr float PanelGap = 6.0F;
        constexpr float HierarchyWidth = 260.0F;
        constexpr float InspectorWidth = 310.0F;
        constexpr float AssetsHeight = 230.0F;
        constexpr float StatusBarHeight = 24.0F;
        constexpr float ToolbarHeight = 32.0F;
        constexpr float OpenSceneDialogWidth = 560.0F;
        constexpr float OpenSceneDialogHeight = 420.0F;

        [[nodiscard]] std::vector<const EditorAssetRecord*> BuildSceneAssetList(const EditorAssetDatabase& assetDatabase)
        {
            std::vector<const EditorAssetRecord*> sceneAssets;
            for (const auto& pair : assetDatabase.GetAssetsByID())
            {
                const EditorAssetRecord& asset = pair.second;
                if (asset.type == EditorAssetType::Scene)
                {
                    sceneAssets.push_back(&asset);
                }
            }

            std::sort(sceneAssets.begin(),
                      sceneAssets.end(),
                      [](const EditorAssetRecord* left, const EditorAssetRecord* right)
                      {
                          VE_ASSERT(left != nullptr);
                          VE_ASSERT(right != nullptr);
                          return left->path.GetString() < right->path.GetString();
                      });
            return sceneAssets;
        }
    } // namespace

    void ProjectEditingView::Init(Editor& editor)
    {
        if (initialized_)
        {
            return;
        }

        hierarchyPanel_.Init(editor);
        sceneViewPanel_.Init(editor);
        gameViewPanel_.Init(editor);
        assetsPanel_.Init(editor);
        inspectorPanel_.Init(editor);
        initialized_ = true;
    }

    void ProjectEditingView::Render(Editor& editor)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        RenderMainMenu(editor);
        const float menuBarHeight = ImGui::GetFrameHeight();
        RenderToolbar(editor, ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + menuBarHeight), ImVec2(viewport->WorkSize.x, ToolbarHeight));
        RenderOpenSceneDialog(editor);
        projectDirectoryDialog_.Render(editor);
        buildPackageDialog_.Render(editor);

        const float contentTop = viewport->WorkPos.y + menuBarHeight + ToolbarHeight + PanelGap;
        const float statusBarY = viewport->WorkPos.y + viewport->WorkSize.y - StatusBarHeight;
        const float contentHeight = (std::max)(0.0F, statusBarY - contentTop - PanelGap);
        const ImVec2 origin(viewport->WorkPos.x, contentTop);
        const ImVec2 available(viewport->WorkSize.x, contentHeight);
        const float centerWidth = (std::max)(320.0F, available.x - HierarchyWidth - InspectorWidth - PanelGap * 2.0F);
        const float centerHeight = (std::max)(240.0F, available.y - AssetsHeight - PanelGap);
        const float splitCenterWidth = (std::max)(160.0F, (centerWidth - PanelGap) * 0.5F);

        hierarchyPanel_.Render(origin, ImVec2(HierarchyWidth, available.y));

        const float centerX = origin.x + HierarchyWidth + PanelGap;
        sceneViewPanel_.Render(ImVec2(centerX, origin.y), ImVec2(splitCenterWidth, centerHeight));
        gameViewPanel_.Render(ImVec2(centerX + splitCenterWidth + PanelGap, origin.y), ImVec2(splitCenterWidth, centerHeight));
        assetsPanel_.Render(ImVec2(centerX, origin.y + centerHeight + PanelGap), ImVec2(centerWidth, AssetsHeight));

        inspectorPanel_.Render(ImVec2(centerX + centerWidth + PanelGap, origin.y), ImVec2(InspectorWidth, available.y));

        RenderStatusBar(editor, ImVec2(viewport->WorkPos.x, statusBarY), ImVec2(viewport->WorkSize.x, StatusBarHeight));
    }

    std::shared_ptr<RTRenderTexture> ProjectEditingView::GetSceneViewTexture() const noexcept
    {
        return sceneViewPanel_.GetSceneViewTexture().GetRTRenderTexture();
    }

    RTCameraInitParam ProjectEditingView::GetSceneViewCameraInitParam() const noexcept
    {
        return sceneViewPanel_.GetSceneViewCameraInitParam();
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

    void ProjectEditingView::RenderMainMenu(Editor& editor)
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("Project"))
        {
            if (ImGui::MenuItem("Open Scene...", nullptr, false, !editor.IsPlaying()))
            {
                openSceneDialogRequested_ = true;
            }

            if (ImGui::MenuItem("Save Scene", nullptr, false, editor.CanSaveCurrentScene()))
            {
                editor.SaveCurrentScene();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Open Project...", nullptr, false, !editor.IsPlaying()))
            {
                projectDirectoryDialog_.RequestOpen();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Build"))
        {
#if VE_PLATFORM_MACOS
            if (ImGui::MenuItem("Package macOS...", nullptr, false, !editor.IsPlaying()))
#else
            if (ImGui::MenuItem("Package Windows...", nullptr, false, !editor.IsPlaying()))
#endif
            {
                buildPackageDialog_.RequestOpen();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scripts"))
        {
            if (ImGui::MenuItem("Recompile Scripts", nullptr, false, !editor.IsPlaying()))
            {
                editor.RecompileScripts();
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    void ProjectEditingView::RenderToolbar(Editor& editor, const ImVec2& position, const ImVec2& size)
    {
        constexpr ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                                                 ImGuiWindowFlags_NoScrollWithMouse;
        constexpr float ButtonWidth = 72.0F;

        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0F, 4.0F));
        if (!ImGui::Begin("EditorToolbar", nullptr, WindowFlags))
        {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        const float buttonX = (std::max)(0.0F, (ImGui::GetContentRegionAvail().x - ButtonWidth) * 0.5F);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonX);

        if (editor.IsPlaying())
        {
            if (ImGui::Button("Stop", ImVec2(ButtonWidth, 0.0F)))
            {
                editor.StopPlay();
            }
        }
        else
        {
            const bool canStartPlay = editor.CanStartPlay();
            if (!canStartPlay)
            {
                ImGui::BeginDisabled();
            }

            if (ImGui::Button("Play", ImVec2(ButtonWidth, 0.0F)))
            {
                editor.StartPlay();
            }

            if (!canStartPlay)
            {
                ImGui::EndDisabled();
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ProjectEditingView::RenderOpenSceneDialog(Editor& editor)
    {
        if (openSceneDialogRequested_)
        {
            openSceneDialogRequested_ = false;
            openSceneSelectedPath_ = editor.GetCurrentScenePath();
            ImGui::OpenPopup("Open Scene");
        }

        ImGui::SetNextWindowSize(ImVec2(OpenSceneDialogWidth, OpenSceneDialogHeight), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        const std::vector<const EditorAssetRecord*> sceneAssets = BuildSceneAssetList(editor.GetAssetDatabase());
        if (sceneAssets.empty())
        {
            ImGui::TextDisabled("No scene assets found.");
        }
        else
        {
            ImGui::BeginChild("OpenSceneList", ImVec2(OpenSceneDialogWidth, OpenSceneDialogHeight - 86.0F), true);
            for (const EditorAssetRecord* sceneAsset : sceneAssets)
            {
                VE_ASSERT(sceneAsset != nullptr);
                const bool selected = openSceneSelectedPath_ == sceneAsset->path;
                if (ImGui::Selectable(sceneAsset->path.GetString().c_str(), selected))
                {
                    openSceneSelectedPath_ = sceneAsset->path;
                }
            }
            ImGui::EndChild();
        }

        const bool canOpen = !openSceneSelectedPath_.IsEmpty();
        if (!canOpen)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Open", ImVec2(96.0F, 0.0F)))
        {
            editor.OpenScene(openSceneSelectedPath_);
            ImGui::CloseCurrentPopup();
        }

        if (!canOpen)
        {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96.0F, 0.0F)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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
