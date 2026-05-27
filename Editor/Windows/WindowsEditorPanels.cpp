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

    void WindowsEditorPanels::ResetSelection() noexcept
    {
        selectedGameObjectId_ = InvalidSceneObjectId;
    }

    void WindowsEditorPanels::DrawViewportSurface(const char* label, const EditorProjectService& projectService)
    {
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x = std::max(canvasSize.x, 1.0f);
        canvasSize.y = std::max(canvasSize.y, 1.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 background = ImGui::GetColorU32(ImVec4(0.035f, 0.04f, 0.045f, 1.0f));
        const ImU32 border = ImGui::GetColorU32(ImVec4(0.20f, 0.22f, 0.24f, 1.0f));
        drawList->AddRectFilled(canvasPos,
                                ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                                background);
        drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), border);

        const std::string sceneText = projectService.HasCurrentScene()
                                          ? projectService.GetCurrentScenePath().GetString()
                                          : std::string("No scene");
        const std::string text = std::string(label) + "  " + sceneText;
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        drawList->AddText(ImVec2(canvasPos.x + (canvasSize.x - textSize.x) * 0.5f,
                                 canvasPos.y + (canvasSize.y - textSize.y) * 0.5f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          text.c_str());
        ImGui::Dummy(canvasSize);
    }

    void WindowsEditorPanels::PrepareSceneMutation(EngineRuntime& runtime)
    {
        runtime.GetGameThreadSystem().ClearActiveScene();
    }

    void WindowsEditorPanels::FinishSceneMutation(EditorProjectService& projectService)
    {
        projectService.GetCurrentEditScene().UpdateTransforms();
        projectService.MarkDirty();
    }
} // namespace ve
