#pragma once

#include "Editor/Core/EditorEventDispatcher.h"
#include "Editor/Core/EditorEvents.h"
#include "Editor/Panels/BasePanel.h"

namespace ve
{
    class CameraComponent;
    class DotnetScriptableComponent;
    class GameObject;
    class LightComponent;
    class MeshRenderComponent;
    class TransformComponent;
} // namespace ve

namespace ve::editor
{
    class Editor;
    struct EditorAssetRecord;

    class InspectorPanel final : public BasePanel
    {
    public:
        void Init(Editor& editor) override;

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
        void OnSelectionChanged(const EditorSelectionChangedEvent& event);
        void RenderGameObject(GameObject& gameObject);
        void RenderTransformComponent(TransformComponent& transform);
        void RenderMeshRenderComponent(MeshRenderComponent& mesh);
        void RenderCameraComponent(CameraComponent& camera);
        void RenderLightComponent(LightComponent& light);
        [[nodiscard]] bool RenderScriptComponent(GameObject& gameObject, DotnetScriptableComponent& script);
        void RenderAddComponent(GameObject& gameObject);
        void RenderAsset(const EditorAssetRecord& asset);
        void RenderMaterialAsset(const EditorAssetRecord& asset);

        Editor* editor_ = nullptr;
        EditorSelectionChangedEvent selection_;
        EditorEventSubscription selectionSubscription_;
    };
} // namespace ve::editor
