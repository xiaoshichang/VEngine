#pragma once

#include "Editor/Panels/BasePanel.h"

namespace ve::editor
{
    class Editor;

    class AssetsPanel final : public BasePanel
    {
    public:
        void Render(Editor& editor, const ImVec2& position, const ImVec2& size);

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;

        Editor* editor_ = nullptr;
    };
} // namespace ve::editor
