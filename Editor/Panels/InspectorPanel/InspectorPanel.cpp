#include "Editor/Panels/InspectorPanel/InspectorPanel.h"

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorAssetDatabase.h"
#include "Editor/Panels/InspectorPanel/InspectorPanelInternal.h"
#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/ColliderComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/RigidbodyComponent.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <string_view>
#include <vector>

namespace ve::editor
{
    void InspectorPanel::Init(Editor& editor)
    {
        editor_ = &editor;
        selection_ = editor.GetSelection();
        if (!selectionSubscription_.IsValid())
        {
            selectionSubscription_ = editor.GetEventDispatcher().Subscribe<EditorSelectionChangedEvent>([this](const EditorSelectionChangedEvent& event)
                                                                                                        { OnSelectionChanged(event); });
        }
    }

    const char* InspectorPanel::GetName() const noexcept
    {
        return "Inspector";
    }

    void InspectorPanel::RenderContent()
    {
        if (editor_ == nullptr)
        {
            return;
        }

        switch (selection_.selectionType)
        {
        case EditorSelectionType::GameObject:
        {
            GameObject* selectedGameObject = selection_.gameObject;
            if (selectedGameObject != nullptr)
            {
                RenderGameObject(*selectedGameObject);
            }
            return;
        }
        case EditorSelectionType::Asset:
        {
            const EditorAssetRecord* asset = editor_->GetAssetDatabase().FindAsset(selection_.assetPath);
            if (asset != nullptr)
            {
                RenderAsset(*asset);
            }
            else
            {
                ImGui::TextDisabled("Selected asset was not found.");
            }
            return;
        }
        case EditorSelectionType::None:
            break;
        }

        ImGui::TextDisabled("Nothing selected.");
    }

    void InspectorPanel::OnSelectionChanged(const EditorSelectionChangedEvent& event)
    {
        selection_ = event;
    }

    void InspectorPanel::RenderGameObject(GameObject& gameObject)
    {
        std::array<char, TextBufferSize> nameBuffer = ToTextBuffer(gameObject.GetName());
        if (RenderFieldInputText("Name", nameBuffer.data(), nameBuffer.size()))
        {
            gameObject.SetName(nameBuffer.data());
        }

        ImGui::TextDisabled("%zu component(s)", static_cast<size_t>(gameObject.GetComponentCount()));
        ImGui::Separator();

        if (TransformComponent* transform = gameObject.GetComponent<TransformComponent>(); transform != nullptr)
        {
            RenderTransformComponent(*transform);
        }

        if (MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>(); mesh != nullptr)
        {
            if (RenderMeshRenderComponent(gameObject, *mesh))
            {
                return;
            }
        }

        if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr)
        {
            if (RenderCameraComponent(gameObject, *camera))
            {
                return;
            }
        }

        if (LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr)
        {
            if (RenderLightComponent(gameObject, *light))
            {
                return;
            }
        }

        if (ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>(); collider != nullptr)
        {
            if (RenderColliderComponent(gameObject, *collider))
            {
                return;
            }
        }

        if (RigidbodyComponent* rigidbody = gameObject.GetComponent<RigidbodyComponent>(); rigidbody != nullptr)
        {
            if (RenderRigidbodyComponent(gameObject, *rigidbody))
            {
                return;
            }
        }

        for (SizeT scriptIndex = 0; scriptIndex < gameObject.GetScriptableComponentCount();)
        {
            ScriptableComponent* script = gameObject.GetScriptableComponent(scriptIndex);
            DotnetScriptableComponent* dotnetScript = dynamic_cast<DotnetScriptableComponent*>(script);
            if (dotnetScript == nullptr)
            {
                ++scriptIndex;
                continue;
            }

            if (RenderScriptComponent(gameObject, *dotnetScript))
            {
                continue;
            }

            ++scriptIndex;
        }

        RenderAddComponent(gameObject);
    }

    void InspectorPanel::RenderAddComponent(GameObject& gameObject)
    {
        if (editor_ == nullptr)
        {
            return;
        }

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float buttonWidth = (std::min)(AddComponentButtonWidth, availableWidth);
        const float buttonOffset = (std::max)(0.0f, (availableWidth - buttonWidth) * 0.5f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonOffset);
        if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0.0f)))
        {
            addComponentFilter_[0] = '\0';
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (!ImGui::BeginPopup("AddComponentPopup"))
        {
            return;
        }

        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText("Filter", addComponentFilter_, sizeof(addComponentFilter_));
        ImGui::Separator();

        const std::string_view filter(addComponentFilter_);
        bool renderedAnyComponent = false;

        auto renderAddEntry = [&](const char* label, auto addComponent)
        {
            if (!MatchesFilter(label, filter))
            {
                return;
            }

            renderedAnyComponent = true;
            if (ImGui::Selectable(label))
            {
                addComponent();
                ImGui::CloseCurrentPopup();
            }
        };

        if (gameObject.GetComponent<MeshRenderComponent>() == nullptr)
        {
            renderAddEntry("Mesh Renderer",
                           [&gameObject]()
                           {
                               Result<MeshRenderComponent*> result = gameObject.AddComponent<MeshRenderComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add mesh renderer component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        if (gameObject.GetComponent<CameraComponent>() == nullptr)
        {
            renderAddEntry("Camera",
                           [&gameObject]()
                           {
                               Result<CameraComponent*> result = gameObject.AddComponent<CameraComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add camera component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        if (gameObject.GetComponent<LightComponent>() == nullptr)
        {
            renderAddEntry("Light",
                           [&gameObject]()
                           {
                               Result<LightComponent*> result = gameObject.AddComponent<LightComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add light component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        if (gameObject.GetComponent<ColliderComponent>() == nullptr)
        {
            renderAddEntry("Collider",
                           [&gameObject]()
                           {
                               Result<ColliderComponent*> result = gameObject.AddComponent<ColliderComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add collider component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        if (gameObject.GetComponent<RigidbodyComponent>() == nullptr)
        {
            renderAddEntry("Rigidbody",
                           [&gameObject]()
                           {
                               Result<RigidbodyComponent*> result = gameObject.AddComponent<RigidbodyComponent>();
                               if (!result)
                               {
                                   VE_LOG_WARN_CATEGORY("Editor", "Failed to add rigidbody component: {}", result.GetError().GetMessage());
                               }
                           });
        }

        const std::vector<ScriptTypeInfo>& scriptTypes = editor_->GetScriptDatabase().GetScriptTypes();
        for (const ScriptTypeInfo& scriptType : scriptTypes)
        {
            if (HasDotnetScriptType(gameObject, scriptType.typeName))
            {
                continue;
            }

            const char* label = scriptType.displayName.empty() ? scriptType.typeName.c_str() : scriptType.displayName.c_str();
            if (!MatchesFilter(label, filter) && !MatchesFilter(scriptType.typeName, filter))
            {
                continue;
            }

            renderedAnyComponent = true;
            if (ImGui::Selectable(label))
            {
                ScriptingSystem& scriptingSystem = editor_->GetRuntime().GetScriptingSystem();
                Result<DotnetScriptableComponent*> result =
                    gameObject.AddComponentWithoutRenderRegistration<DotnetScriptableComponent>(scriptType.typeName, scriptingSystem);
                if (!result)
                {
                    VE_LOG_WARN_CATEGORY("Editor", "Failed to add script component '{}': {}", scriptType.typeName, result.GetError().GetMessage());
                }
                else
                {
                    const ErrorCode ensureResult = result.GetValue()->EnsureScriptInstance(false);
                    if (ensureResult != ErrorCode::None)
                    {
                        VE_LOG_WARN_CATEGORY("Editor", "Failed to create editor script instance '{}': {}", scriptType.typeName, ToString(ensureResult));
                    }
                }
                ImGui::CloseCurrentPopup();
            }
        }

        if (!renderedAnyComponent)
        {
            ImGui::TextDisabled("No components available.");
        }

        ImGui::EndPopup();
    }
} // namespace ve::editor
