#pragma once

#include "Editor/Panels/BasePanel/BasePanel.h"

namespace ve
{
    class GameObject;
    class Scene;
} // namespace ve

namespace ve::editor
{
    class Editor;

    class HierarchyPanel final : public BasePanel
    {
    public:
        void Init(Editor& editor) override;

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
        [[nodiscard]] bool RenderGameObjectTree(Scene& scene, GameObject& gameObject);
        void RenderSceneContextMenu(Scene& scene);
        [[nodiscard]] bool RenderGameObjectContextMenu(Scene& scene, GameObject& gameObject);
        void CreateGameObject(Scene& scene, GameObject* parent);
        [[nodiscard]] bool RemoveGameObject(Scene& scene, GameObject& gameObject);

        Editor* editor_ = nullptr;
    };
} // namespace ve::editor
