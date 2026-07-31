#include "Engine/Runtime/Profiling/ProfileSystem.h"

#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Render/RenderPerformanceStatistics.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Time/Time.h"

#include <cstddef>

#if VE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")
#elif VE_PLATFORM_MACOS
#include <mach/mach.h>
#endif

namespace ve
{
    namespace
    {
        constexpr std::array<ProfileMetricDescriptor, static_cast<SizeT>(ProfileMetricId::Count)> MetricDescriptors = {{
            {ProfileMetricId::CommonFrameTime, "Common", "Frame time", "ms"},
            {ProfileMetricId::CommonTotalMemory, "Common", "Total memory", "MB"},
            {ProfileMetricId::RenderDrawCall, "Render", "DrawCall", "calls"},
            {ProfileMetricId::RenderVsmTotalPhysicalPages, "Render", "VSM total physical pages", "pages"},
            {ProfileMetricId::RenderVsmAllocatedPhysicalPages, "Render", "VSM allocated physical pages", "pages"},
            {ProfileMetricId::RenderVsmRequestedPages, "Render", "VSM requested pages", "pages"},
            {ProfileMetricId::RenderVsmCachedPages, "Render", "VSM cached pages", "pages"},
            {ProfileMetricId::RenderVsmNewlyAllocatedPages, "Render", "VSM newly allocated pages", "pages"},
            {ProfileMetricId::RenderVsmRedrawnPages, "Render", "VSM redrawn pages", "pages"},
            {ProfileMetricId::RenderVsmUnmappedPages, "Render", "VSM unmapped pages", "pages"},
        }};

        [[nodiscard]] Float64 QueryTotalMemoryMegabytes() noexcept
        {
#if VE_PLATFORM_WINDOWS
            PROCESS_MEMORY_COUNTERS counters = {};
            counters.cb = sizeof(counters);
            if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != FALSE)
            {
                return static_cast<Float64>(counters.WorkingSetSize) / (1024.0 * 1024.0);
            }
#elif VE_PLATFORM_MACOS
            mach_task_basic_info_data_t info = {};
            mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
            if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
            {
                return static_cast<Float64>(info.resident_size) / (1024.0 * 1024.0);
            }
#endif
            return 0.0;
        }
    } // namespace

    ProfileSystem::~ProfileSystem()
    {
        Shutdown();
    }

    ErrorCode ProfileSystem::Initialize(TimeSystem& timeSystem, RenderSystem& renderSystem)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        timeSystem_ = &timeSystem;
        renderSystem_ = &renderSystem;
        for (MetricState& metric : metrics_)
        {
            metric.recordingEnabled = false;
            metric.latestValue = 0.0;
            metric.samples.clear();
            metric.samples.reserve(ProfileMaximumSamples);
        }
        initialized_ = true;
        recordingEnabled_ = false;
        return ErrorCode::None;
    }

    void ProfileSystem::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        for (MetricState& metric : metrics_)
        {
            metric.samples.clear();
            metric.recordingEnabled = false;
            metric.latestValue = 0.0;
        }
        timeSystem_ = nullptr;
        renderSystem_ = nullptr;
        recordingEnabled_ = false;
        initialized_ = false;
    }

    bool ProfileSystem::IsInitialized() const noexcept
    {
        return initialized_;
    }

    void ProfileSystem::SetRecordingEnabled(bool enabled) noexcept
    {
        if (recordingEnabled_ == enabled)
        {
            return;
        }

        recordingEnabled_ = enabled;
    }

    bool ProfileSystem::IsRecordingEnabled() const noexcept
    {
        return initialized_ && recordingEnabled_;
    }

    void ProfileSystem::ClearSamples() noexcept
    {
        for (MetricState& metric : metrics_)
        {
            metric.samples.clear();
            metric.latestValue = 0.0;
        }
    }

    void ProfileSystem::RecordFrameEnd() noexcept
    {
        if (!initialized_ || timeSystem_ == nullptr || renderSystem_ == nullptr)
        {
            return;
        }

        if (!recordingEnabled_)
        {
            return;
        }

        const TimeSnapshot time = timeSystem_->GetSnapshot();
        const RenderPerformanceStatistics render = renderSystem_->GetPerformanceStatistics();
        const Float64 frameTimeMilliseconds = static_cast<Float64>(time.deltaSeconds) * 1000.0;
        const Float64 totalMemory = QueryTotalMemoryMegabytes();
        const Float64 drawCall = static_cast<Float64>(renderSystem_->GetRecordedDrawCallCount());

        AppendSample(ProfileMetricId::CommonFrameTime, time.frameIndex, frameTimeMilliseconds);
        AppendSample(ProfileMetricId::CommonTotalMemory, time.frameIndex, totalMemory);
        AppendSample(ProfileMetricId::RenderDrawCall, time.frameIndex, drawCall);
        AppendSample(ProfileMetricId::RenderVsmTotalPhysicalPages, time.frameIndex, render.virtualShadow.totalPhysicalPages);
        AppendSample(ProfileMetricId::RenderVsmAllocatedPhysicalPages, time.frameIndex, render.virtualShadow.allocatedPhysicalPages);
        AppendSample(ProfileMetricId::RenderVsmRequestedPages, time.frameIndex, render.virtualShadow.requestedPages);
        AppendSample(ProfileMetricId::RenderVsmCachedPages, time.frameIndex, render.virtualShadow.cachedPages);
        AppendSample(ProfileMetricId::RenderVsmNewlyAllocatedPages, time.frameIndex, render.virtualShadow.newlyAllocatedPages);
        AppendSample(ProfileMetricId::RenderVsmRedrawnPages, time.frameIndex, render.virtualShadow.redrawnPages);
        AppendSample(ProfileMetricId::RenderVsmUnmappedPages, time.frameIndex, render.virtualShadow.unmappedPages);
    }

    void ProfileSystem::SetMetricRecordingEnabled(ProfileMetricId id, bool enabled) noexcept
    {
        if (id == ProfileMetricId::Count)
        {
            return;
        }
        metrics_[ToIndex(id)].recordingEnabled = enabled;
    }

    bool ProfileSystem::IsMetricRecordingEnabled(ProfileMetricId id) const noexcept
    {
        return id != ProfileMetricId::Count && metrics_[ToIndex(id)].recordingEnabled;
    }

    std::span<const ProfileMetricDescriptor> ProfileSystem::GetMetricDescriptors() const noexcept
    {
        return MetricDescriptors;
    }

    std::span<const ProfileSample> ProfileSystem::GetSamples(ProfileMetricId id) const noexcept
    {
        if (id == ProfileMetricId::Count)
        {
            return {};
        }
        return metrics_[ToIndex(id)].samples;
    }

    Float64 ProfileSystem::GetLatestValue(ProfileMetricId id) const noexcept
    {
        return id == ProfileMetricId::Count ? 0.0 : metrics_[ToIndex(id)].latestValue;
    }

    SizeT ProfileSystem::ToIndex(ProfileMetricId id) noexcept
    {
        return static_cast<SizeT>(id);
    }

    void ProfileSystem::AppendSample(ProfileMetricId id, UInt64 frameIndex, Float64 value) noexcept
    {
        MetricState& metric = metrics_[ToIndex(id)];
        if (!metric.recordingEnabled)
        {
            return;
        }

        metric.latestValue = value;
        if (metric.samples.size() >= ProfileMaximumSamples)
        {
            metric.samples.erase(metric.samples.begin());
        }
        metric.samples.push_back(ProfileSample{frameIndex, value});
    }
} // namespace ve
