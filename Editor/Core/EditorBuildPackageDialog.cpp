#include "Editor/Core/EditorBuildPackageDialog.h"

#include "Editor/Core/Editor.h"

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
        packager_.Reset();
    }

    void EditorBuildPackageDialog::Render(Editor& editor)
    {
        if (!isOpen_)
        {
            return;
        }

        ImGui::OpenPopup("Package Windows");
        if (!ImGui::BeginPopupModal("Package Windows", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        if (!hasStarted_)
        {
            packager_.StartWindowsPackage(editor);
            hasStarted_ = true;
        }

        if (packager_.GetStatus() == PackageRunStatus::Running)
        {
            packager_.Advance(editor);
        }

        ImGui::SetNextItemWidth(BuildPackageDialogWidth);
        ImGui::TextUnformatted("Windows");
        ImGui::TextWrapped("%s", packager_.GetStatusMessage().c_str());

        ImGui::ProgressBar(packager_.GetProgress(), ImVec2(BuildPackageProgressWidth, 0.0F));
        ImGui::Separator();

        for (const PackageStepState& step : packager_.GetSteps())
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
        if (!packager_.GetOutputPath().empty())
        {
            ImGui::TextWrapped("Output: %s", packager_.GetOutputPath().c_str());
        }

        if (!packager_.GetLogPath().empty())
        {
            ImGui::TextWrapped("Log: %s", packager_.GetLogPath().c_str());
        }

        const bool canClose = packager_.GetStatus() != PackageRunStatus::Running;
        if (!canClose)
        {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Close", ImVec2(120.0F, 0.0F)))
        {
            isOpen_ = false;
            hasStarted_ = false;
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
} // namespace ve::editor
