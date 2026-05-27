#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

namespace ve
{
    void WindowsEditorPanels::DrawGameView(const EditorProjectService& projectService)
    {
        DrawViewportSurface("GameView", projectService);
    }
} // namespace ve
