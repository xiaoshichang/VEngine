#include "Editor/Core/EditorBuildPackageDialog.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Core/Platform.h"

#include <imgui.h>

namespace ve::editor
{
    namespace
    {
        constexpr float BuildPackageDialogWidth = 760.0F;
        constexpr float BuildPackageProgressWidth = 720.0F;
    } // namespace

    void EditorBuildPackageDialog::RequestOpen()
    {
        isOpen_ = true;
        hasStarted_ = false;
        packer_ = CreateEditorProjectPackerForHostPlatform();
        if (packer_ != nullptr)
        {
            packer_->Reset();
        }
    }

    void EditorBuildPackageDialog::Render(Editor& editor)
    {
        if (!isOpen_ || packer_ == nullptr)
        {
            return;
        }

        ImGui::OpenPopup("Package Project");
        if (!ImGui::BeginPopupModal("Package Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        if (!hasStarted_)
        {
            packer_->Start(editor);
            hasStarted_ = true;
        }

        if (packer_->GetStatus() == PackageRunStatus::Running)
        {
            packer_->Advance(editor);
        }

        ImGui::SetNextItemWidth(BuildPackageDialogWidth);
        ImGui::TextUnformatted(GetHostPlatformText());
        ImGui::TextWrapped("%s", packer_->GetStatusMessage().c_str());

        ImGui::ProgressBar(packer_->GetProgress(), ImVec2(BuildPackageProgressWidth, 0.0F));
        ImGui::Separator();

        for (const PackageStepState& step : packer_->GetSteps())
        {
            ImGui::Text("%s", ToStatusText(step.status));
            ImGui::SameLine(110.0F);
            ImGui::TextUnformatted(step.name.c_str());

            if (!step.message.empty() && step.status == PackageStepStatus::Failed)
            {
                ImGui::SameLine(390.0F);
                ImGui::TextWrapped("%s", step.message.c_str());
            }
        }

        ImGui::Separator();
        if (!packer_->GetOutputPath().empty())
        {
            ImGui::TextWrapped("Output: %s", packer_->GetOutputPath().c_str());
        }

        if (!packer_->GetLogPath().empty())
        {
            ImGui::TextWrapped("Log: %s", packer_->GetLogPath().c_str());
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

        ImGui::EndPopup();
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
} // namespace ve::editor
