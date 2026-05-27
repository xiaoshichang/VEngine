#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

namespace ve
{
    void WindowsEditorPanels::DrawSceneView(const EditorProjectService& projectService)
    {
        DrawViewportSurface("SceneView", projectService);
    }
} // namespace ve
