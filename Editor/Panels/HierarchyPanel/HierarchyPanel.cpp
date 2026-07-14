#include "Editor/Panels/HierarchyPanel/HierarchyPanel.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneSystem.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] bool ContainsGameObject(GameObject& root, const GameObject* candidate) noexcept
        {
            if (&root == candidate)
            {
                return true;
            }

            TransformComponent* transform = root.GetComponent<TransformComponent>();
            if (transform == nullptr)
            {
                return false;
            }

            for (SizeT index = 0; index < transform->GetChildCount(); ++index)
            {
                GameObject* child = transform->GetChildGameObject(index);
                if (child != nullptr && ContainsGameObject(*child, candidate))
                {
                    return true;
                }
            }

            return false;
        }
    } // namespace

    void HierarchyPanel::Init(Editor& editor)
    {
        editor_ = &editor;
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
        RenderSceneContextMenu(*scene);

        if (!sceneOpen)
        {
            if (ImGui::BeginPopupContextWindow("HierarchyBlankContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("Add GameObject"))
                {
                    CreateGameObject(*scene, nullptr);
                }
                ImGui::EndPopup();
            }
            return;
        }

        if (scene->GetRootGameObjectCount() == 0)
        {
            ImGui::TextDisabled("Empty scene.");
            ImGui::TreePop();
            if (ImGui::BeginPopupContextWindow("HierarchyBlankContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("Add GameObject"))
                {
                    CreateGameObject(*scene, nullptr);
                }
                ImGui::EndPopup();
            }
            return;
        }

        for (SizeT index = 0; index < scene->GetRootGameObjectCount();)
        {
            GameObject* gameObject = scene->GetRootGameObject(index);
            if (gameObject != nullptr)
            {
                if (RenderGameObjectTree(*scene, *gameObject))
                {
                    continue;
                }
            }
            ++index;
        }

        ImGui::TreePop();

        if (ImGui::BeginPopupContextWindow("HierarchyBlankContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Add GameObject"))
            {
                CreateGameObject(*scene, nullptr);
            }
            ImGui::EndPopup();
        }
    }

    bool HierarchyPanel::RenderGameObjectTree(Scene& scene, GameObject& gameObject)
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
        if (RenderGameObjectContextMenu(scene, gameObject))
        {
            if (hasChildren && open)
            {
                ImGui::TreePop();
            }
            return true;
        }

        if (!hasChildren)
        {
            return false;
        }

        if (open)
        {
            for (SizeT index = 0; index < transform->GetChildCount();)
            {
                GameObject* child = transform->GetChildGameObject(index);
                if (child != nullptr)
                {
                    if (RenderGameObjectTree(scene, *child))
                    {
                        continue;
                    }
                }
                ++index;
            }

            ImGui::TreePop();
        }

        return false;
    }

    void HierarchyPanel::RenderSceneContextMenu(Scene& scene)
    {
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Add GameObject"))
            {
                CreateGameObject(scene, nullptr);
            }
            ImGui::EndPopup();
        }
    }

    bool HierarchyPanel::RenderGameObjectContextMenu(Scene& scene, GameObject& gameObject)
    {
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Add GameObject"))
            {
                CreateGameObject(scene, &gameObject);
            }

            bool removed = false;
            if (ImGui::MenuItem("Remove GameObject"))
            {
                removed = RemoveGameObject(scene, gameObject);
            }
            ImGui::EndPopup();
            return removed;
        }

        return false;
    }

    void HierarchyPanel::CreateGameObject(Scene& scene, GameObject* parent)
    {
        Result<GameObject*> result = scene.CreateGameObject("GameObject", parent);
        if (!result)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to add GameObject: {}", result.GetError().GetMessage());
            return;
        }

        editor_->SetSelectedGameObject(result.GetValue());
    }

    bool HierarchyPanel::RemoveGameObject(Scene& scene, GameObject& gameObject)
    {
        if (ContainsGameObject(gameObject, editor_->GetSelectedGameObject()))
        {
            editor_->ClearSelection();
        }

        TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
        TransformComponent* parent = transform != nullptr ? transform->GetParent() : nullptr;
        if (parent != nullptr)
        {
            return parent->DestroyChild(gameObject);
        }

        return scene.DestroyRootGameObject(gameObject);
    }
} // namespace ve::editor
