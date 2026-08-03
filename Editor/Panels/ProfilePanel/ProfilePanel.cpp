#include "Editor/Panels/ProfilePanel/ProfilePanel.h"

#include "Editor/Core/Editor.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <imgui.h>
#include <string>

namespace ve::editor
{
    void ProfilePanel::Init(Editor& editor)
    {
        editor_ = &editor;
        chartValues_.resize(static_cast<SizeT>(ProfileMetricId::Count));
        chartSamples_.resize(static_cast<SizeT>(ProfileMetricId::Count));
    }

    const char* ProfilePanel::GetName() const noexcept
    {
        return "Profile";
    }

    void ProfilePanel::RenderContent()
    {
        if (editor_ == nullptr)
        {
            ImGui::TextDisabled("Profile system is unavailable.");
            return;
        }

        ProfileSystem& profileSystem = editor_->GetRuntime().GetProfileSystem();
        const bool recordingEnabled = profileSystem.IsRecordingEnabled();
        if (ImGui::Button(recordingEnabled ? "Stop Capture" : "Start Capture"))
        {
            profileSystem.SetRecordingEnabled(!recordingEnabled);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(recordingEnabled ? "Capturing" : "Capture disabled");

        const std::span<const ProfileMetricDescriptor> descriptors = profileSystem.GetMetricDescriptors();
        RenderMetricGroup("Common", descriptors);
        RenderMetricGroup("Render", descriptors);

        if (lastChartRefreshTime_ < 0.0 || ImGui::GetTime() - lastChartRefreshTime_ >= 1.0)
        {
            RefreshCharts();
            lastChartRefreshTime_ = ImGui::GetTime();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Charts refresh every 1 second. History: up to 3000 samples.");
        for (const ProfileMetricDescriptor& descriptor : descriptors)
        {
            if (!selected_[static_cast<SizeT>(descriptor.id)] || chartSamples_[static_cast<SizeT>(descriptor.id)].empty())
            {
                continue;
            }

            RenderMetricChart(descriptor, profileSystem);
        }
    }

    void ProfilePanel::RenderMetricGroup(const char* groupName, std::span<const ProfileMetricDescriptor> descriptors)
    {
        if (!ImGui::CollapsingHeader(groupName, ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ProfileSystem& profileSystem = editor_->GetRuntime().GetProfileSystem();
        if (ImGui::BeginTable(groupName, 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
        {
            for (const ProfileMetricDescriptor& descriptor : descriptors)
            {
                if (descriptor.group != groupName)
                {
                    continue;
                }

                ImGui::TableNextColumn();
                bool& selected = selected_[static_cast<SizeT>(descriptor.id)];
                const std::string checkboxLabel = std::string(descriptor.name) + "##ProfileMetric" + std::to_string(static_cast<UInt32>(descriptor.id));
                if (ImGui::Checkbox(checkboxLabel.c_str(), &selected))
                {
                    profileSystem.SetMetricRecordingEnabled(descriptor.id, selected);
                    if (!selected)
                    {
                        chartValues_[static_cast<SizeT>(descriptor.id)].clear();
                        chartSamples_[static_cast<SizeT>(descriptor.id)].clear();
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    void ProfilePanel::RenderMetricChart(const ProfileMetricDescriptor& descriptor, const ProfileSystem& profileSystem)
    {
        const SizeT index = static_cast<SizeT>(descriptor.id);
        const Float64 latestValue = profileSystem.GetLatestValue(descriptor.id);
        const std::vector<ProfileSample>& samples = chartSamples_[index];
        const std::string chartLabel = std::string(descriptor.name) + " (" + std::string(descriptor.unit) + ")  Current: " + std::to_string(latestValue);
        ImGui::TextUnformatted(chartLabel.c_str());
        const ImVec2 plotSize(ImGui::GetContentRegionAvail().x, 140.0F);
        const ImVec2 plotPosition = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(("##ProfilePlot" + std::to_string(static_cast<UInt32>(descriptor.id))).c_str(), plotSize);
        const ImVec2 plotEnd(plotPosition.x + plotSize.x, plotPosition.y + plotSize.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_Separator);
        const ImU32 lineColor = ImGui::GetColorU32(ImGuiCol_PlotLines);
        const float left = plotPosition.x + 52.0F;
        const float right = plotEnd.x - 8.0F;
        const float top = plotPosition.y + 4.0F;
        const float bottom = plotEnd.y - 22.0F;
        drawList->AddLine(ImVec2(left, top), ImVec2(left, bottom), borderColor);
        drawList->AddLine(ImVec2(left, bottom), ImVec2(right, bottom), borderColor);

        Float64 minimum = samples.front().value;
        Float64 maximum = minimum;
        for (const ProfileSample& sample : samples)
        {
            minimum = (std::min)(minimum, sample.value);
            maximum = (std::max)(maximum, sample.value);
        }
        if (std::abs(maximum - minimum) < 0.000001)
        {
            maximum = minimum + 1.0;
        }
        for (int gridIndex = 1; gridIndex <= 3; ++gridIndex)
        {
            const float y = top + (bottom - top) * static_cast<float>(gridIndex) / 4.0F;
            drawList->AddLine(ImVec2(left, y), ImVec2(right, y), gridColor);
        }
        const Float64 frameRange = static_cast<Float64>(samples.back().frameIndex - samples.front().frameIndex);
        const auto getPoint = [&](const ProfileSample& sample) {
            const float normalizedX = frameRange > 0.0
                ? static_cast<float>(static_cast<Float64>(sample.frameIndex - samples.front().frameIndex) / frameRange)
                : 0.5F;
            const float x = left + normalizedX * (right - left);
            const float y = bottom - static_cast<float>((sample.value - minimum) / (maximum - minimum)) * (bottom - top);
            return ImVec2(x, y);
        };
        for (SizeT sampleIndex = 1; sampleIndex < samples.size(); ++sampleIndex)
        {
            const ProfileSample& previous = samples[sampleIndex - 1];
            const ProfileSample& current = samples[sampleIndex];
            drawList->AddLine(getPoint(previous), getPoint(current), lineColor, 1.5F);
        }
        drawList->AddText(ImVec2(plotPosition.x, top), borderColor, std::to_string(maximum).c_str());
        drawList->AddText(ImVec2(plotPosition.x, bottom - ImGui::GetTextLineHeight()), borderColor, std::to_string(minimum).c_str());
        drawList->AddText(ImVec2(left, bottom + 4.0F), borderColor, ("Frame " + std::to_string(samples.front().frameIndex)).c_str());
        const std::string lastFrameLabel = "Frame " + std::to_string(samples.back().frameIndex);
        drawList->AddText(ImVec2(right - ImGui::CalcTextSize(lastFrameLabel.c_str()).x, bottom + 4.0F), borderColor, lastFrameLabel.c_str());

        if (ImGui::IsItemHovered())
        {
            const ImVec2 mousePosition = ImGui::GetIO().MousePos;
            if (mousePosition.x >= left && mousePosition.x <= right && mousePosition.y >= top && mousePosition.y <= bottom)
            {
                const float normalizedX = (mousePosition.x - left) / (right - left);
                const SizeT sampleIndex = static_cast<SizeT>(std::clamp(normalizedX, 0.0F, 1.0F) * static_cast<float>(samples.size() - 1));
                const ProfileSample& sample = samples[sampleIndex];
                const ImVec2 point = getPoint(sample);
                drawList->AddCircleFilled(point, 4.0F, ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));
                ImGui::BeginTooltip();
                ImGui::Text("Frame: %llu", static_cast<unsigned long long>(sample.frameIndex));
                ImGui::Text("Value: %.3f %s", sample.value, std::string(descriptor.unit).c_str());
                ImGui::EndTooltip();
            }
        }
        ImGui::TextDisabled("X: Frame    Y: %s", std::string(descriptor.unit).c_str());
    }

    void ProfilePanel::RefreshCharts()
    {
        ProfileSystem& profileSystem = editor_->GetRuntime().GetProfileSystem();
        for (const ProfileMetricDescriptor& descriptor : profileSystem.GetMetricDescriptors())
        {
            const SizeT index = static_cast<SizeT>(descriptor.id);
            if (!selected_[index])
            {
                continue;
            }

            const std::span<const ProfileSample> samples = profileSystem.GetSamples(descriptor.id);
            std::vector<Float32>& values = chartValues_[index];
            std::vector<ProfileSample>& chartSamples = chartSamples_[index];
            values.clear();
            chartSamples.assign(samples.begin(), samples.end());
            values.reserve(samples.size());
            for (const ProfileSample& sample : samples)
            {
                values.push_back(static_cast<Float32>(sample.value));
            }
        }
    }
} // namespace ve::editor
