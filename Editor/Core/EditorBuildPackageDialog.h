#pragma once

#include "Editor/Core/EditorProjectPacker.h"
#include "Engine/Runtime/Core/NonCopyable.h"

#include <array>
#include <memory>
#include <string>

namespace ve::editor
{
    class Editor;

    class EditorBuildPackageDialog : public NonMovable
    {
    public:
        EditorBuildPackageDialog() = default;

        void RequestOpen();
        void RequestOpen(PackageTargetPlatform targetPlatform);
        void Render(Editor& editor);

    private:
        void LoadSettings(Editor& editor);
        [[nodiscard]] ErrorCode SaveSettings(Editor& editor);
        void ApplySettingsToInputs();
        void ApplyInputsToSettings();
        void RenderPrePackageView(Editor& editor);
        void RenderProgressView(Editor& editor);
        void RenderPlatformSettings();
        void RenderReadonlyField(const char* label, const std::string& text, float width) const;
        void RenderReadonlyMultiline(const char* label, const std::string& text, float width, float height) const;

        [[nodiscard]] std::string GetDialogTitle() const;
        [[nodiscard]] std::string GetPlatformBuildRootPreview(Editor& editor) const;
        [[nodiscard]] const char* GetPlatformName() const noexcept;
        [[nodiscard]] static const char* GetPlatformDisplayName(PackageTargetPlatform platform) noexcept;
        [[nodiscard]] static const char* ToStatusText(PackageStepStatus status) noexcept;
        [[nodiscard]] static const char* GetHostPlatformText() noexcept;
        static void CopyToBuffer(std::array<char, 256>& buffer, const std::string& text);
        [[nodiscard]] static std::string BufferToString(const std::array<char, 256>& buffer);

        std::unique_ptr<EditorProjectPacker> packer_;
        EditorProjectDescriptor projectDescriptor_;
        EditorProjectDescriptor::BuildSettings buildSettings_;
        PackageTargetPlatform targetPlatform_ = PackageTargetPlatform::Mac;
        std::array<char, 256> macBundleIdentifierBuffer_{};
        std::array<char, 256> macCmakeBuildConfigBuffer_{};
        std::array<char, 256> iosBundleIdentifierBuffer_{};
        std::array<char, 256> iosDevelopmentTeamBuffer_{};
        std::array<char, 256> iosProvisioningProfileSpecifierBuffer_{};
        std::array<char, 256> iosCodeSignIdentityBuffer_{};
        std::array<char, 256> iosDeploymentTargetBuffer_{};
        std::array<char, 256> windowsConfigurationBuffer_{};
        std::string settingsMessage_;
        bool isOpen_ = false;
        bool hasStarted_ = false;
        bool settingsLoaded_ = false;
        int iosSDKIndex_ = 0;
        int iosCodeSignStyleIndex_ = 0;
        int iosExportMethodIndex_ = 0;
        int iosOrientationIndex_ = 0;
    };
} // namespace ve::editor
