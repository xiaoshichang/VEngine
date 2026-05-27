#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Application/EngineRuntime.h"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace ve
{
    WindowsEditorPanels::WindowsEditorPanels()
    {
        RegisterSceneReflectionTypes(reflectionRegistry_);
    }

    void WindowsEditorPanels::BeginFrame()
    {
        viewportRequests_.clear();
    }

    EditorViewportFrameData WindowsEditorPanels::ConsumeViewportFrameData()
    {
        EditorViewportFrameData frameData;
        frameData.viewports = std::move(viewportRequests_);
        viewportRequests_.clear();
        return frameData;
    }

    void WindowsEditorPanels::ResetSelection() noexcept
    {
        selectedGameObjectId_ = InvalidSceneObjectId;
    }

    void WindowsEditorPanels::PrepareSceneMutation(EngineRuntime& runtime)
    {
        runtime.GetGameThreadSystem().ClearActiveScene();
    }

    void WindowsEditorPanels::FinishSceneMutation(EditorProjectService& projectService)
    {
        projectService.GetActiveScene().UpdateTransforms();
        projectService.MarkActiveSceneEdited();
    }
} // namespace ve
