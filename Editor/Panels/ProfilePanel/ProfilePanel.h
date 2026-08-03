#pragma once

#include "Editor/Panels/BasePanel/BasePanel.h"

#include "Engine/Runtime/Profiling/ProfileSystem.h"

#include <array>
#include <vector>

namespace ve::editor
{
    class ProfilePanel final : public BasePanel
    {
    public:
        void Init(Editor& editor) override;

    private:
        [[nodiscard]] const char* GetName() const noexcept override;
        void RenderContent() override;
        void RenderMetricGroup(const char* groupName, std::span<const ProfileMetricDescriptor> descriptors);
        void RenderMetricChart(const ProfileMetricDescriptor& descriptor, const ProfileSystem& profileSystem);
        void RefreshCharts();

        Editor* editor_ = nullptr;
        std::array<bool, static_cast<SizeT>(ProfileMetricId::Count)> selected_{};
        std::vector<std::vector<Float32>> chartValues_;
        std::vector<std::vector<ProfileSample>> chartSamples_;
        Float64 lastChartRefreshTime_ = -1.0;
    };
} // namespace ve::editor
