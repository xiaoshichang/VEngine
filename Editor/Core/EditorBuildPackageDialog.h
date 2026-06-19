#pragma once

#include "Editor/Core/EditorProjectPackager.h"
#include "Engine/Runtime/Core/NonCopyable.h"

namespace ve::editor
{
    class Editor;

    class EditorBuildPackageDialog : public NonMovable
    {
    public:
        EditorBuildPackageDialog() = default;

        void RequestOpen();
        void Render(Editor& editor);

    private:
        [[nodiscard]] static const char* ToStatusText(PackageStepStatus status) noexcept;

        EditorProjectPackager packager_;
        bool isOpen_ = false;
        bool hasStarted_ = false;
    };
} // namespace ve::editor
