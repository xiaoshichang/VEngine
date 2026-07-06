#pragma once

#include "Editor/Core/EditorProjectPacker.h"

#include <string>
#include <string_view>

namespace ve::editor
{
    class EditorProjectPackerIOS final : public EditorProjectPacker
    {
    public:
        EditorProjectPackerIOS() = default;

    private:
        [[nodiscard]] const char* GetPlatformName() const noexcept override;
        [[nodiscard]] std::string GetRunningStatusMessage() const override;
        [[nodiscard]] std::string GetSucceededStatusMessage() const override;
        void ConfigurePackagePaths() override;
        void InitializeSteps() override;
        [[nodiscard]] ErrorCode RunStep(size_t stepIndex, Editor& editor) override;
        void ResetPlatformState() override;

        [[nodiscard]] ErrorCode ValidateIOSPackagingEnvironment(Editor& editor);
        [[nodiscard]] ErrorCode PrepareIOSPackageDirectories();
        [[nodiscard]] ErrorCode PublishIOSNativeAOTScripts(Editor& editor);
        [[nodiscard]] ErrorCode WriteIOSNativeAOTScriptManifest();
        [[nodiscard]] ErrorCode ConfigureIOSXcodeProject();
        [[nodiscard]] ErrorCode ArchiveIOSPlayer();
        [[nodiscard]] ErrorCode ExportIOSArchive();
        [[nodiscard]] ErrorCode PrepareIOSSimulatorPackage();
        [[nodiscard]] ErrorCode WriteIOSExportOptionsPlist();
        [[nodiscard]] ErrorCode WriteIOSPackageInfo();
        [[nodiscard]] ErrorCode RunShellCommand(const std::string& command);
        [[nodiscard]] ErrorCode RunPreflightCommand(const std::string& command, const std::string& failureMessage);
        [[nodiscard]] std::string BuildCMakeConfigureCommand() const;
        [[nodiscard]] std::string BuildNativeAOTPublishCommand() const;
        [[nodiscard]] std::string BuildXcodeArchiveCommand() const;
        [[nodiscard]] std::string BuildXcodeSimulatorBuildCommand() const;
        [[nodiscard]] std::string BuildXcodeExportCommand() const;
        [[nodiscard]] Path GetGeneratedIOSNativeAOTScriptProjectPath() const;
        [[nodiscard]] Path GetScriptHostProjectPath() const;
        [[nodiscard]] Path FindPublishedNativeAOTLibrary() const;
        [[nodiscard]] Path FindNativeAOTRuntimeNativeDirectory() const;
        [[nodiscard]] std::string GetNativeAOTAssemblyName() const;
        [[nodiscard]] std::string GetNativeAOTRuntimeIdentifier() const;
        [[nodiscard]] std::string GetNativeAOTRuntimePackageName() const;
        [[nodiscard]] Path GetIOSSimulatorAppBundlePath() const;
        [[nodiscard]] bool IsSimulatorBuild() const noexcept;
        [[nodiscard]] bool ShouldAllowProvisioningUpdates() const noexcept;
        [[nodiscard]] std::string GetExportSigningStyle() const;

        [[nodiscard]] static std::string ShellQuote(std::string_view text);
        [[nodiscard]] static std::string SanitizeBundleIdentifierSegment(std::string text);
        [[nodiscard]] static bool IsValidBundleIdentifier(std::string_view text) noexcept;
        [[nodiscard]] static bool IsValidCodeSignStyle(std::string_view text) noexcept;
        [[nodiscard]] static bool IsValidExportMethod(std::string_view text) noexcept;
        [[nodiscard]] static bool IsValidIOSSDK(std::string_view text) noexcept;
        [[nodiscard]] static bool IsValidOrientation(std::string_view text) noexcept;
        [[nodiscard]] static bool IsValidDeploymentTarget(std::string_view text) noexcept;
        [[nodiscard]] static std::string NormalizeCodeSignStyle(std::string text);

        Path stagingRoot_;
        Path xcodeBinaryRoot_;
        Path archivePath_;
        Path ipaExportRoot_;
        Path exportOptionsPlistPath_;
        Path nativeAOTProjectRoot_;
        Path nativeAOTOutputRoot_;
        Path nativeAOTLibraryPath_;
        Path nativeAOTRuntimeNativeRoot_;
        std::string bundleIdentifier_;
        std::string iosSDK_;
        std::string buildConfiguration_;
        std::string developmentTeam_;
        std::string codeSignStyle_;
        std::string provisioningProfileSpecifier_;
        std::string codeSignIdentity_;
        std::string deploymentTarget_;
        std::string exportMethod_;
        std::string orientation_;
    };
} // namespace ve::editor
