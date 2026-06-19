#include "Editor/Panels/HierarchyPanel.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneSystem.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <imgui.h>

namespace ve::editor
{
    void HierarchyPanel::Render(Editor& editor, const ImVec2& position, const ImVec2& size)
    {
        editor_ = &editor;
        BasePanel::Render(position, size);
        editor_ = nullptr;
    }

    const char* HierarchyPanel::GetName() const noexcept
    {
        return "Hierarchy";
    }

    void HierarchyPanel::RenderContent()
    {
        if (editor_ == nullptr)
        {
            return;
        }

        Scene* scene = editor_->GetSceneSystem().GetScene();
        if (scene == nullptr)
        {
            ImGui::TextDisabled("No scene.");
            return;
        }

        const std::string& sceneName = scene->GetName();
        const char* sceneLabel = sceneName.empty() ? "Scene" : sceneName.c_str();
        const ImGuiTreeNodeFlags sceneFlags =
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        const bool sceneOpen = ImGui::TreeNodeEx(static_cast<void*>(scene), sceneFlags, "%s", sceneLabel);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            editor_->SetSelectedGameObject(nullptr);
        }

        if (!sceneOpen)
        {
            return;
        }

        if (scene->GetRootGameObjectCount() == 0)
        {
            ImGui::TextDisabled("Empty scene.");
            ImGui::TreePop();
            return;
        }

        for (SizeT index = 0; index < scene->GetRootGameObjectCount(); ++index)
        {
            GameObject* gameObject = scene->GetRootGameObject(index);
            if (gameObject != nullptr)
            {
                RenderGameObjectTree(*gameObject);
            }
        }

        ImGui::TreePop();
    }

    void HierarchyPanel::RenderGameObjectTree(GameObject& gameObject)
    {
        TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
        const bool hasChildren = transform != nullptr && transform->GetChildCount() > 0;

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (editor_->GetSelectedGameObject() == &gameObject)
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        if (!hasChildren)
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const std::string& name = gameObject.GetName();
        const char* label = name.empty() ? "GameObject" : name.c_str();
        const bool open = ImGui::TreeNodeEx(static_cast<void*>(&gameObject), flags, "%s", label);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            editor_->SetSelectedGameObject(&gameObject);
        }

        if (!hasChildren)
        {
            return;
        }

        if (open)
        {
            for (SizeT index = 0; index < transform->GetChildCount(); ++index)
            {
                GameObject* child = transform->GetChildGameObject(index);
                if (child != nullptr)
                {
                    RenderGameObjectTree(*child);
                }
            }

            ImGui::TreePop();
        }
    }
} // namespace ve::editor
