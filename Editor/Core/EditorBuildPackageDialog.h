#pragma once

#include "Editor/Core/EditorProjectPacker.h"
#include "Engine/Runtime/Core/NonCopyable.h"

#include <memory>
#include <vector>

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
        [[nodiscard]] static const char* GetHostPlatformText() noexcept;

        std::unique_ptr<EditorProjectPacker> packer_;
        std::vector<PackageTargetPlatformDesc> availableTargets_;
        size_t selectedTargetIndex_ = 0;
        bool isOpen_ = false;
        bool hasStarted_ = false;
    };
} // namespace ve::editor
