#pragma once

#include "Editor/Panels/BasePanel.h"

namespace ve
{
    class GameObject;
}

namespace ve::editor
{
    class Editor;

    class HierarchyPanel final : public BasePanel
    {
    public:
        void Render(Editor& editor, const ImVec2& position, const ImVec2& size);

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
        void RenderGameObjectTree(ve::GameObject& gameObject);

        Editor* editor_ = nullptr;
    };
} // namespace ve::editor
