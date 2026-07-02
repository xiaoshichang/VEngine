#pragma once

#include "Editor/Core/EditorProjectPacker.h"

#include <string_view>

namespace ve::editor
{
    class EditorProjectPackerMac final : public EditorProjectPacker
    {
    public:
        EditorProjectPackerMac() = default;

    private:
        [[nodiscard]] const char* GetPlatformName() const noexcept override;
        [[nodiscard]] std::string GetRunningStatusMessage() const override;
        [[nodiscard]] std::string GetSucceededStatusMessage() const override;
        void ConfigurePackagePaths() override;
        void InitializeSteps() override;
        [[nodiscard]] ErrorCode RunStep(size_t stepIndex, Editor& editor) override;
        void ResetPlatformState() override;

        [[nodiscard]] ErrorCode PrepareMacBundleDirectories();
        [[nodiscard]] ErrorCode ConfigureMacXcodeProject();
        [[nodiscard]] ErrorCode BuildMacPlayer();
        [[nodiscard]] ErrorCode CopyMacPlayerExecutable();
        [[nodiscard]] ErrorCode CopyMacPlayerManagedRuntime();
        [[nodiscard]] ErrorCode WriteMacInfoPlist();
        [[nodiscard]] ErrorCode SignMacAppBundle();
        [[nodiscard]] ErrorCode WriteMacPackageInfo();
        [[nodiscard]] ErrorCode RunShellCommand(const std::string& command);
        [[nodiscard]] std::string BuildCMakeConfigureCommand() const;
        [[nodiscard]] std::string BuildCMakeBuildCommand() const;
        [[nodiscard]] Path GetMacPlayerBuildOutputPath() const;

        [[nodiscard]] static std::string ShellQuote(std::string_view text);

        Path appContentsRoot_;
        Path appResourcesRoot_;
        Path cmakeSourceRoot_;
        Path cmakeBinaryRoot_;
        std::string cmakeBuildConfig_;
    };
} // namespace ve::editor
