#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Asset/AssetDatabase.h"

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
        selectionKind_ = SelectionKind::None;
        selectedGameObjectId_ = InvalidSceneObjectId;
        selectedAssetPath_ = {};
    }

    void WindowsEditorPanels::SelectGameObject(SceneObjectId gameObjectId) noexcept
    {
        selectionKind_ = SelectionKind::GameObject;
        selectedGameObjectId_ = gameObjectId;
        selectedAssetPath_ = {};
    }

    Path WindowsEditorPanels::GetAssetSelectionPath(const AssetRecord& record) const
    {
        return record.assetType == AssetType::SourceModel && !record.source.IsEmpty() ? record.source : record.path;
    }

    void WindowsEditorPanels::SelectAssetRecord(const AssetRecord& record)
    {
        selectionKind_ = SelectionKind::Asset;
        selectedGameObjectId_ = InvalidSceneObjectId;
        selectedAssetPath_ = GetAssetSelectionPath(record);
    }

    bool WindowsEditorPanels::IsAssetSelected(const AssetRecord& record) const noexcept
    {
        return selectionKind_ == SelectionKind::Asset && selectedAssetPath_ == GetAssetSelectionPath(record);
    }

    const AssetRecord*
    WindowsEditorPanels::FindSelectedAssetRecord(const EditorProjectService& projectService) const noexcept
    {
        if (selectionKind_ != SelectionKind::Asset || selectedAssetPath_.IsEmpty())
        {
            return nullptr;
        }

        return projectService.GetAssetDatabase().FindAssetByPath(selectedAssetPath_);
    }

    void WindowsEditorPanels::PrepareSceneMutation(EditorProjectService& projectService, EngineRuntime& runtime)
    {
        if (projectService.IsPlaying())
        {
            runtime.GetGameThreadSystem().ClearActiveScene();
        }
    }

    void WindowsEditorPanels::FinishSceneMutation(EditorProjectService& projectService)
    {
        projectService.GetActiveScene().UpdateTransforms();
        projectService.MarkActiveSceneEdited();
    }
} // namespace ve
