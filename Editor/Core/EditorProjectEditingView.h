#pragma once

#include "Editor/Core/Gizmos.h"
#include "Editor/Core/EditorBuildPackageDialog.h"
#include "Editor/Core/EditorProjectDirectoryDialog.h"
#include "Editor/Panels/AssetsPanel.h"
#include "Editor/Panels/GameViewPanel.h"
#include "Editor/Panels/HierarchyPanel.h"
#include "Editor/Panels/InspectorPanel.h"
#include "Editor/Panels/SceneViewPanel.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>

struct ImVec2;

namespace ve::editor
{
    class Editor;

    class ProjectEditingView : public NonMovable
    {
    public:
        ProjectEditingView() = default;

        void Init(Editor& editor);
        void Render(Editor& editor);
        [[nodiscard]] std::shared_ptr<RTRenderTexture> GetSceneViewTexture() const noexcept;
        [[nodiscard]] RTCameraDesc GetSceneViewCameraDesc() const noexcept;
        [[nodiscard]] rhi::RhiFillMode GetSceneViewFillMode() const noexcept;
        [[nodiscard]] bool IsSceneViewGridEnabled() const noexcept;
        [[nodiscard]] Float32 GetSceneViewGridOpacity() const noexcept;
        [[nodiscard]] Float32 GetSceneViewGridUnitSize() const noexcept;
        [[nodiscard]] const Gizmos& GetSceneViewGizmos() const noexcept;
        [[nodiscard]] Matrix44 GetSceneViewCameraLocalToWorld() const noexcept;
        [[nodiscard]] std::shared_ptr<RTRenderTexture> GetGameViewTexture() const noexcept;

    private:
        void RenderMainMenu(Editor& editor);
        void RenderOpenSceneDialog(Editor& editor);
        void RenderStatusBar(Editor& editor, const ImVec2& position, const ImVec2& size);

        HierarchyPanel hierarchyPanel_;
        SceneViewPanel sceneViewPanel_;
        GameViewPanel gameViewPanel_;
        InspectorPanel inspectorPanel_;
        AssetsPanel assetsPanel_;
        ProjectDirectoryDialog projectDirectoryDialog_;
        EditorBuildPackageDialog buildPackageDialog_;
        Path openSceneSelectedPath_;
        bool openSceneDialogRequested_ = false;
        bool initialized_ = false;
    };
} // namespace ve::editor
