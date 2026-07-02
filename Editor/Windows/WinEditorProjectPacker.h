#pragma once

#include "Editor/Core/EditorProjectPacker.h"

namespace ve::editor
{
    class EditorProjectPackerWin final : public EditorProjectPacker
    {
    public:
        EditorProjectPackerWin() = default;

    private:
        [[nodiscard]] const char* GetPlatformName() const noexcept override;
        [[nodiscard]] std::string GetRunningStatusMessage() const override;
        [[nodiscard]] std::string GetSucceededStatusMessage() const override;
        void ConfigurePackagePaths() override;
        void InitializeSteps() override;
        [[nodiscard]] ErrorCode RunStep(size_t stepIndex, Editor& editor) override;

        [[nodiscard]] ErrorCode CopyWindowsPlayerExecutable();
        [[nodiscard]] ErrorCode CopyWindowsPlayerManagedRuntime();
        [[nodiscard]] ErrorCode WriteWindowsPackageInfo();
    };
} // namespace ve::editor
