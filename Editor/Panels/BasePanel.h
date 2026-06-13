#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"

struct ImVec2;

namespace ve::editor
{
    class BasePanel : public NonMovable
    {
    public:
        BasePanel() = default;
        virtual ~BasePanel() = default;

        void Render(const ImVec2& position, const ImVec2& size);

    protected:
        [[nodiscard]] virtual const char* GetName() const noexcept = 0;
        virtual void RenderContent() = 0;
    };
} // namespace ve::editor
