#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace ve
{
    class RenderSystem;
    class TimeSystem;

    inline constexpr SizeT ProfileMaximumSamples = 3000;

    enum class ProfileMetricId : UInt32
    {
        CommonFrameTime,
        CommonTotalMemory,
        RenderDrawCall,
        RenderVsmTotalPhysicalPages,
        RenderVsmAllocatedPhysicalPages,
        RenderVsmRequestedPages,
        RenderVsmCachedPages,
        RenderVsmNewlyAllocatedPages,
        RenderVsmRedrawnPages,
        RenderVsmUnmappedPages,
        Count,
    };

    struct ProfileMetricDescriptor
    {
        ProfileMetricId id = ProfileMetricId::CommonFrameTime;
        std::string_view group;
        std::string_view name;
        std::string_view unit;
    };

    struct ProfileSample
    {
        UInt64 frameIndex = 0;
        Float64 value = 0.0;
    };

    class ProfileSystem final : public NonMovable
    {
    public:
        ProfileSystem() = default;
        ~ProfileSystem();

        [[nodiscard]] ErrorCode Initialize(TimeSystem& timeSystem, RenderSystem& renderSystem);
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        void SetRecordingEnabled(bool enabled) noexcept;
        [[nodiscard]] bool IsRecordingEnabled() const noexcept;
        void ClearSamples() noexcept;

        void RecordFrameEnd() noexcept;
        void SetMetricRecordingEnabled(ProfileMetricId id, bool enabled) noexcept;
        [[nodiscard]] bool IsMetricRecordingEnabled(ProfileMetricId id) const noexcept;
        [[nodiscard]] std::span<const ProfileMetricDescriptor> GetMetricDescriptors() const noexcept;
        [[nodiscard]] std::span<const ProfileSample> GetSamples(ProfileMetricId id) const noexcept;
        [[nodiscard]] Float64 GetLatestValue(ProfileMetricId id) const noexcept;

    private:
        struct MetricState
        {
            bool recordingEnabled = false;
            Float64 latestValue = 0.0;
            std::vector<ProfileSample> samples;
        };

        [[nodiscard]] static SizeT ToIndex(ProfileMetricId id) noexcept;
        void AppendSample(ProfileMetricId id, UInt64 frameIndex, Float64 value) noexcept;

        TimeSystem* timeSystem_ = nullptr;
        RenderSystem* renderSystem_ = nullptr;
        std::array<MetricState, static_cast<SizeT>(ProfileMetricId::Count)> metrics_;
        bool initialized_ = false;
        bool recordingEnabled_ = false;
    };
} // namespace ve
