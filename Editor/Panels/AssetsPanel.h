#pragma once

#include "Editor/Panels/BasePanel.h"

namespace ve::editor
{
    class AssetsPanel final : public BasePanel
    {
    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
    };
} // namespace ve::editor
