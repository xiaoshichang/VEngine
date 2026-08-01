#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <string>

namespace ve::editor
{
    class Editor;

    class EditorFooter final : public NonMovable
    {
    public:
        [[nodiscard]] Float32 GetContentBottom() const noexcept;
        void Render(Editor& editor);

    private:
        Float32 frameRate_ = 0.0F;
        Float64 lastRefreshTime_ = -1.0;
        std::string lastLogLine_;
    };
} // namespace ve::editor
