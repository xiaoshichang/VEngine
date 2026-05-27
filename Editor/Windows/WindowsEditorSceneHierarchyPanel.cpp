#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <imgui.h>

#include <cstdint>
#include <string>

namespace ve
{
    void WindowsEditorPanels::DrawSceneHierarchy(EditorProjectService& projectService,
                                                 EngineRuntime& runtime,
                                                 std::string& statusMessage)
    {
        ImGui::TextUnformatted("Scene Hierarchy");
        ImGui::SameLine();
        ImGui::TextDisabled("%llu object(s)",
                            static_cast<unsigned long long>(
                                projectService.GetActiveScene().GetGameObjectCount()));
        ImGui::Separator();

        for (GameObject* root : projectService.GetActiveScene().GetRootGameObjects())
        {
            DrawGameObjectNode(*root);
        }

        const bool hasSelection =
            projectService.GetActiveScene().FindGameObject(selectedGameObjectId_) != nullptr;
        if (ImGui::BeginPopupContextWindow("SceneHierarchyContextMenu", ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem("Create Empty"))
            {
                PrepareSceneMutation(runtime);
                GameObject& gameObject = projectService.GetActiveScene().CreateGameObject("GameObject");
                gameObject.AddComponent<TransformComponent>();
                selectedGameObjectId_ = gameObject.GetId();
                FinishSceneMutation(projectService);
                statusMessage = "Created GameObject.";
            }

            if (ImGui::MenuItem("Delete Selected", nullptr, false, hasSelection))
            {
                if (GameObject* selected = projectService.GetActiveScene().FindGameObject(selectedGameObjectId_))
                {
                    PrepareSceneMutation(runtime);
                    (void)projectService.GetActiveScene().DestroyGameObject(*selected);
                    selectedGameObjectId_ = InvalidSceneObjectId;
                    FinishSceneMutation(projectService);
                    statusMessage = "Deleted GameObject.";
                }
            }

            ImGui::EndPopup();
        }
    }

    void WindowsEditorPanels::DrawGameObjectNode(GameObject& gameObject)
    {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (gameObject.GetChildren().empty())
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        if (selectedGameObjectId_ == gameObject.GetId())
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        const bool opened =
            ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(gameObject.GetId())),
                              flags,
                              "%s",
                              gameObject.GetName().empty() ? "GameObject" : gameObject.GetName().c_str());
        const bool clickedForSelection = ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
                                         ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (clickedForSelection && !ImGui::IsItemToggledOpen())
        {
            selectedGameObjectId_ = gameObject.GetId();
        }

        if (opened && !gameObject.GetChildren().empty())
        {
            for (GameObject* child : gameObject.GetChildren())
            {
                DrawGameObjectNode(*child);
            }

            ImGui::TreePop();
        }
    }
} // namespace ve
