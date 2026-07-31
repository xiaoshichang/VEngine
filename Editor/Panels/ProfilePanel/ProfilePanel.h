#pragma once

#include "Editor/Panels/BasePanel/BasePanel.h"

namespace ve::editor
{
    class ProfilePanel final : public BasePanel
    {
    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
    };
} // namespace ve::editor
