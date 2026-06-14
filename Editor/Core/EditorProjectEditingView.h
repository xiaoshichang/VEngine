#pragma once

#include "Editor/Core/EditorProjectDirectoryDialog.h"
#include "Editor/Panels/AssetsPanel.h"
#include "Editor/Panels/GameViewPanel.h"
#include "Editor/Panels/HierarchyPanel.h"
#include "Editor/Panels/InspectorPanel.h"
#include "Editor/Panels/SceneViewPanel.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>

namespace ve::editor
{
    class Editor;

    class ProjectEditingView : public NonMovable
    {
    public:
        ProjectEditingView() = default;

        void Init(Editor& editor);
        void Render(Editor& editor);
        [[nodiscard]] std::shared_ptr<RTRenderTexture> GetGameViewTexture() const noexcept;

    private:
        void RenderMainMenu();

        HierarchyPanel hierarchyPanel_;
        SceneViewPanel sceneViewPanel_;
        GameViewPanel gameViewPanel_;
        InspectorPanel inspectorPanel_;
        AssetsPanel assetsPanel_;
        ProjectDirectoryDialog projectDirectoryDialog_;
        bool initialized_ = false;
    };
} // namespace ve::editor
