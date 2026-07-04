#include "Editor/Core/EditorBuildPackageDialog.h"

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/Platform.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace ve::editor
{
    namespace
    {
        constexpr float BuildPackageDialogWidth = 760.0F;
        constexpr float BuildPackageProgressWidth = 720.0F;

        constexpr const char* IOSSDKOptions[] = {"iphoneos", "iphonesimulator"};
        constexpr const char* IOSCodeSignStyles[] = {"Automatic", "Manual"};
        constexpr const char* IOSExportMethods[] = {"development", "ad-hoc", "app-store", "enterprise"};

        [[nodiscard]] int FindOptionIndex(const char* const* options, int optionCount, const std::string& value, int fallback)
        {
            for (int optionIndex = 0; optionIndex < optionCount; ++optionIndex)
            {
                if (value == options[optionIndex])
                {
                    return optionIndex;
                }
            }

            return fallback;
        }

        [[nodiscard]] std::vector<char> MakeInputBuffer(const std::string& text)
        {
            std::vector<char> buffer(text.begin(), text.end());
            buffer.push_back('\0');
            return buffer;
        }
    } // namespace

    void EditorBuildPackageDialog::RequestOpen()
    {
        const std::vector<PackageTargetPlatformDesc> availableTargets = GetAvailableEditorPackageTargets();
        targetPlatform_ = availableTargets.empty() ? PackageTargetPlatform::Mac : availableTargets.front().platform;
        RequestOpen(targetPlatform_);
    }

    void EditorBuildPackageDialog::RequestOpen(PackageTargetPlatform targetPlatform)
    {
        isOpen_ = true;
        hasStarted_ = false;
        settingsLoaded_ = false;
        targetPlatform_ = targetPlatform;

        settingsMessage_.clear();
        packer_.reset();
    }

    void EditorBuildPackageDialog::Render(Editor& editor)
    {
        if (!isOpen_)
        {
            return;
        }

        if (!settingsLoaded_)
        {
            LoadSettings(editor);
        }

        const std::string title = GetDialogTitle();
        ImGui::OpenPopup(title.c_str());
        if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        if (!hasStarted_)
        {
            RenderPrePackageView(editor);
            ImGui::EndPopup();
            return;
        }

        RenderProgressView(editor);
        ImGui::EndPopup();
    }

    void EditorBuildPackageDialog::LoadSettings(Editor& editor)
    {
        settingsLoaded_ = true;
        projectDescriptor_ = EditorProjectDescriptor{};
        buildSettings_ = EditorProjectDescriptor::BuildSettings{};

        if (editor.GetCurrentProjectPath().empty())
        {
            settingsMessage_ = "No project is open.";
            ApplySettingsToInputs();
            return;
        }

        Result<EditorProjectDescriptor> descriptorResult = EditorProject::LoadDescriptor(Path(editor.GetCurrentProjectPath()));
        if (!descriptorResult)
        {
            settingsMessage_ = "Failed to load project settings: " + descriptorResult.GetError().GetMessage();
            ApplySettingsToInputs();
            return;
        }

        projectDescriptor_ = descriptorResult.GetValue();
        buildSettings_ = projectDescriptor_.buildSettings;
        ApplySettingsToInputs();
    }

    ErrorCode EditorBuildPackageDialog::SaveSettings(Editor& editor)
    {
        if (editor.GetCurrentProjectPath().empty())
        {
            settingsMessage_ = "No project is open.";
            return ErrorCode::InvalidState;
        }

        ApplyInputsToSettings();
        projectDescriptor_.buildSettings = buildSettings_;
        const ErrorCode result = EditorProject::SaveDescriptor(Path(editor.GetCurrentProjectPath()), projectDescriptor_);
        if (result != ErrorCode::None)
        {
            settingsMessage_ = std::string("Failed to save build settings: ") + ToString(result);
            return result;
        }

        settingsMessage_ = "Build settings saved.";
        return ErrorCode::None;
    }

    void EditorBuildPackageDialog::ApplySettingsToInputs()
    {
        CopyToBuffer(macBundleIdentifierBuffer_, buildSettings_.mac.bundleIdentifier);
        CopyToBuffer(macCmakeBuildConfigBuffer_, buildSettings_.mac.cmakeBuildConfig);
        CopyToBuffer(iosBundleIdentifierBuffer_, buildSettings_.ios.bundleIdentifier);
        CopyToBuffer(iosDevelopmentTeamBuffer_, buildSettings_.ios.developmentTeam);
        CopyToBuffer(iosProvisioningProfileSpecifierBuffer_, buildSettings_.ios.provisioningProfileSpecifier);
        CopyToBuffer(iosCodeSignIdentityBuffer_, buildSettings_.ios.codeSignIdentity);
        CopyToBuffer(iosDeploymentTargetBuffer_, buildSettings_.ios.deploymentTarget);
        CopyToBuffer(windowsConfigurationBuffer_, buildSettings_.windows.configuration);

        iosSDKIndex_ = FindOptionIndex(IOSSDKOptions, IM_ARRAYSIZE(IOSSDKOptions), buildSettings_.ios.sdk, 0);
        iosCodeSignStyleIndex_ = FindOptionIndex(IOSCodeSignStyles, IM_ARRAYSIZE(IOSCodeSignStyles), buildSettings_.ios.codeSignStyle, 0);
        iosExportMethodIndex_ = FindOptionIndex(IOSExportMethods, IM_ARRAYSIZE(IOSExportMethods), buildSettings_.ios.exportMethod, 0);
    }

    void EditorBuildPackageDialog::ApplyInputsToSettings()
    {
        buildSettings_.mac.bundleIdentifier = BufferToString(macBundleIdentifierBuffer_);
        buildSettings_.mac.cmakeBuildConfig = BufferToString(macCmakeBuildConfigBuffer_);
        buildSettings_.ios.sdk = IOSSDKOptions[(std::clamp)(iosSDKIndex_, 0, IM_ARRAYSIZE(IOSSDKOptions) - 1)];
        buildSettings_.ios.bundleIdentifier = BufferToString(iosBundleIdentifierBuffer_);
        buildSettings_.ios.developmentTeam = BufferToString(iosDevelopmentTeamBuffer_);
        buildSettings_.ios.codeSignStyle = IOSCodeSignStyles[(std::clamp)(iosCodeSignStyleIndex_, 0, IM_ARRAYSIZE(IOSCodeSignStyles) - 1)];
        buildSettings_.ios.provisioningProfileSpecifier = BufferToString(iosProvisioningProfileSpecifierBuffer_);
        buildSettings_.ios.codeSignIdentity = BufferToString(iosCodeSignIdentityBuffer_);
        buildSettings_.ios.deploymentTarget = BufferToString(iosDeploymentTargetBuffer_);
        buildSettings_.ios.exportMethod = IOSExportMethods[(std::clamp)(iosExportMethodIndex_, 0, IM_ARRAYSIZE(IOSExportMethods) - 1)];
        buildSettings_.windows.configuration = BufferToString(windowsConfigurationBuffer_);
    }

    void EditorBuildPackageDialog::RenderPrePackageView(Editor& editor)
    {
        ImGui::SetNextItemWidth(BuildPackageDialogWidth);
        ImGui::TextUnformatted(GetHostPlatformText());
        ImGui::Separator();

        RenderReadonlyField("Project", editor.GetCurrentProjectName(), BuildPackageDialogWidth);
        RenderReadonlyField("Target", GetPlatformName(), BuildPackageDialogWidth);
        RenderReadonlyField("Output Root", GetPlatformBuildRootPreview(editor), BuildPackageDialogWidth);

        ImGui::Separator();
        RenderPlatformSettings();

        if (!settingsMessage_.empty())
        {
            ImGui::TextWrapped("%s", settingsMessage_.c_str());
        }

        if (ImGui::Button("Save Settings", ImVec2(120.0F, 0.0F)))
        {
            const ErrorCode saveResult = SaveSettings(editor);
            (void)saveResult;
        }

        ImGui::SameLine();
        if (ImGui::Button("Start Package", ImVec2(140.0F, 0.0F)))
        {
            if (SaveSettings(editor) == ErrorCode::None)
            {
                packer_ = CreateEditorProjectPacker(targetPlatform_);
                if (packer_ != nullptr)
                {
                    packer_->Start(editor, buildSettings_);
                }
                hasStarted_ = true;
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(120.0F, 0.0F)))
        {
            isOpen_ = false;
            hasStarted_ = false;
            packer_.reset();
            ImGui::CloseCurrentPopup();
        }
    }

    void EditorBuildPackageDialog::RenderProgressView(Editor& editor)
    {
        if (packer_ == nullptr)
        {
            ImGui::TextUnformatted("Package target is not supported on this host.");
            if (ImGui::Button("Close", ImVec2(120.0F, 0.0F)))
            {
                isOpen_ = false;
                hasStarted_ = false;
                ImGui::CloseCurrentPopup();
            }

            return;
        }

        if (packer_->GetStatus() == PackageRunStatus::Running)
        {
            packer_->Advance(editor);
        }

        ImGui::SetNextItemWidth(BuildPackageDialogWidth);
        ImGui::TextUnformatted(GetHostPlatformText());
        RenderReadonlyField("Status", packer_->GetStatusMessage(), BuildPackageDialogWidth);

        ImGui::ProgressBar(packer_->GetProgress(), ImVec2(BuildPackageProgressWidth, 0.0F));
        ImGui::Separator();

        for (const PackageStepState& step : packer_->GetSteps())
        {
            ImGui::Text("%s", ToStatusText(step.status));
            ImGui::SameLine(110.0F);
            ImGui::TextUnformatted(step.name.c_str());

            if (!step.message.empty() && step.status == PackageStepStatus::Failed)
            {
                RenderReadonlyMultiline("Failed Step", step.message, BuildPackageDialogWidth, ImGui::GetTextLineHeight() * 3.0F);
            }
        }

        ImGui::Separator();
        if (!packer_->GetOutputPath().empty())
        {
            RenderReadonlyField("Output", packer_->GetOutputPath(), BuildPackageDialogWidth);
        }

        if (!packer_->GetLogPath().empty())
        {
            RenderReadonlyField("Log", packer_->GetLogPath(), BuildPackageDialogWidth);
        }

        const bool canClose = packer_->GetStatus() != PackageRunStatus::Running;
        if (!canClose)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Close", ImVec2(120.0F, 0.0F)))
        {
            isOpen_ = false;
            hasStarted_ = false;
            packer_.reset();
            ImGui::CloseCurrentPopup();
        }

        if (!canClose)
        {
            ImGui::EndDisabled();
        }
    }

    void EditorBuildPackageDialog::RenderPlatformSettings()
    {
        switch (targetPlatform_)
        {
        case PackageTargetPlatform::Mac:
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::InputText("Bundle Identifier", macBundleIdentifierBuffer_.data(), macBundleIdentifierBuffer_.size());
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::InputText("CMake Build Config", macCmakeBuildConfigBuffer_.data(), macCmakeBuildConfigBuffer_.size());
            break;
        case PackageTargetPlatform::IOS:
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::Combo("Target SDK", &iosSDKIndex_, IOSSDKOptions, IM_ARRAYSIZE(IOSSDKOptions));
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::InputText("Bundle Identifier", iosBundleIdentifierBuffer_.data(), iosBundleIdentifierBuffer_.size());
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::InputText("Development Team", iosDevelopmentTeamBuffer_.data(), iosDevelopmentTeamBuffer_.size());
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::Combo("Code Sign Style", &iosCodeSignStyleIndex_, IOSCodeSignStyles, IM_ARRAYSIZE(IOSCodeSignStyles));
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::InputText("Provisioning Profile", iosProvisioningProfileSpecifierBuffer_.data(), iosProvisioningProfileSpecifierBuffer_.size());
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::InputText("Code Sign Identity", iosCodeSignIdentityBuffer_.data(), iosCodeSignIdentityBuffer_.size());
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::InputText("Deployment Target", iosDeploymentTargetBuffer_.data(), iosDeploymentTargetBuffer_.size());
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::Combo("Export Method", &iosExportMethodIndex_, IOSExportMethods, IM_ARRAYSIZE(IOSExportMethods));
            break;
        case PackageTargetPlatform::Windows:
            ImGui::SetNextItemWidth(BuildPackageDialogWidth);
            ImGui::InputText("Configuration", windowsConfigurationBuffer_.data(), windowsConfigurationBuffer_.size());
            break;
        }
    }

    void EditorBuildPackageDialog::RenderReadonlyField(const char* label, const std::string& text, float width) const
    {
        std::vector<char> buffer = MakeInputBuffer(text);
        ImGui::SetNextItemWidth(width);
        ImGui::InputText(label, buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);
    }

    void EditorBuildPackageDialog::RenderReadonlyMultiline(const char* label, const std::string& text, float width, float height) const
    {
        std::vector<char> buffer = MakeInputBuffer(text);
        ImGui::SetNextItemWidth(width);
        ImGui::InputTextMultiline(label, buffer.data(), buffer.size(), ImVec2(width, height), ImGuiInputTextFlags_ReadOnly);
    }

    std::string EditorBuildPackageDialog::GetDialogTitle() const
    {
        return std::string("Build ") + GetPlatformDisplayName(targetPlatform_);
    }

    std::string EditorBuildPackageDialog::GetPlatformBuildRootPreview(Editor& editor) const
    {
        if (editor.GetCurrentProjectPath().empty())
        {
            return {};
        }

        return (Path(editor.GetCurrentProjectPath()) / "Build" / GetPlatformName()).GetString();
    }

    const char* EditorBuildPackageDialog::GetPlatformName() const noexcept
    {
        return GetPlatformDisplayName(targetPlatform_);
    }

    const char* EditorBuildPackageDialog::GetPlatformDisplayName(PackageTargetPlatform platform) noexcept
    {
        switch (platform)
        {
        case PackageTargetPlatform::Windows:
            return "Windows";
        case PackageTargetPlatform::Mac:
            return "Mac";
        case PackageTargetPlatform::IOS:
            return "iOS";
        }

        return "Unknown";
    }

    const char* EditorBuildPackageDialog::ToStatusText(PackageStepStatus status) noexcept
    {
        switch (status)
        {
        case PackageStepStatus::Pending:
            return "Pending";
        case PackageStepStatus::Running:
            return "Running";
        case PackageStepStatus::Succeeded:
            return "Done";
        case PackageStepStatus::Failed:
            return "Failed";
        }

        return "Unknown";
    }

    const char* EditorBuildPackageDialog::GetHostPlatformText() noexcept
    {
#if VE_PLATFORM_MACOS
        return "macOS";
#elif VE_PLATFORM_WINDOWS
        return "Windows";
#else
        return "Unsupported";
#endif
    }

    void EditorBuildPackageDialog::CopyToBuffer(std::array<char, 256>& buffer, const std::string& text)
    {
        buffer.fill('\0');
        const size_t copyLength = (std::min)(buffer.size() - 1, text.size());
        std::memcpy(buffer.data(), text.data(), copyLength);
    }

    std::string EditorBuildPackageDialog::BufferToString(const std::array<char, 256>& buffer)
    {
        return std::string(buffer.data());
    }
} // namespace ve::editor
