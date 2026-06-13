#pragma once

#include "Editor/Core/EditorProjectDirectoryDialog.h"
#include "Engine/Runtime/Core/NonCopyable.h"

#include <array>
#include <string>

namespace ve::editor
{
    class Editor;

    class ProjectSelectionView : public NonMovable
    {
    public:
        ProjectSelectionView() = default;

        void Render(Editor& editor);

    private:
        std::array<char, 512> newProjectPathBuffer_{};
        ProjectDirectoryDialog projectDirectoryDialog_;
    };
} // namespace ve::editor
