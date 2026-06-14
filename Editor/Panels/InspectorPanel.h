#pragma once

#include "Editor/Panels/BasePanel.h"

namespace ve
{
    class CameraComponent;
    class GameObject;
    class LightComponent;
    class MeshRenderComponent;
    class TransformComponent;
}

namespace ve::editor
{
    class Editor;
    struct EditorAssetRecord;

    class InspectorPanel final : public BasePanel
    {
    public:
        void Render(Editor& editor, const ImVec2& position, const ImVec2& size);

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
        void RenderGameObject(GameObject& gameObject);
        void RenderTransformComponent(TransformComponent& transform);
        void RenderMeshRenderComponent(MeshRenderComponent& mesh);
        void RenderCameraComponent(CameraComponent& camera);
        void RenderLightComponent(LightComponent& light);
        void RenderAsset(const EditorAssetRecord& asset);

        Editor* editor_ = nullptr;
    };
} // namespace ve::editor
