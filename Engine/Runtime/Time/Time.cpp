#include "Engine/Runtime/Time/Time.h"

#include "Engine/Runtime/Logging/Log.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ve
{
    namespace
    {
        [[nodiscard]] bool IsPositiveFinite(Float32 value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] Float32 ClampRawDelta(Float32 rawDeltaSeconds) noexcept
        {
            if (!std::isfinite(rawDeltaSeconds) || rawDeltaSeconds < 0.0f)
            {
                return 0.0f;
            }

            return rawDeltaSeconds;
        }

        [[nodiscard]] UInt32 CalculateFixedStepCount(Float64 accumulatorSeconds, Float32 fixedDeltaSeconds) noexcept
        {
            if (fixedDeltaSeconds <= 0.0f)
            {
                return 0;
            }

            const Float64 pendingSteps = accumulatorSeconds / static_cast<Float64>(fixedDeltaSeconds);
            if (pendingSteps <= 0.0)
            {
                return 0;
            }

            constexpr Float64 MaxCount = static_cast<Float64>(std::numeric_limits<UInt32>::max());
            return static_cast<UInt32>(std::min(std::floor(pendingSteps), MaxCount));
        }

        [[nodiscard]] Float32 CalculateCurrentFrameRate(Float32 deltaSeconds) noexcept
        {
            if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
            {
                return 0.0f;
            }

            return 1.0f / deltaSeconds;
        }
    } // namespace

    TimeSystem::TimeSystem() = default;

    TimeSystem::~TimeSystem()
    {
        Shutdown();
    }

    ErrorCode TimeSystem::Initialize(const TimeSystemInitParam& initParam)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        if (!IsPositiveFinite(initParam.maxDeltaSeconds) || !IsPositiveFinite(initParam.fixedDeltaSeconds))
        {
            return ErrorCode::InvalidArgument;
        }

        snapshot_ = TimeSnapshot{};
        snapshot_.maxDeltaSeconds = initParam.maxDeltaSeconds;
        snapshot_.fixedDeltaSeconds = initParam.fixedDeltaSeconds;
        frameRateIntervalElapsedSeconds_ = 0.0;
        frameRateIntervalFrameCount_ = 0;
        lastTickTime_ = Clock::now();
        initialized_ = true;
        return ErrorCode::None;
    }

    void TimeSystem::Shutdown() noexcept
    {
        frameRateIntervalElapsedSeconds_ = 0.0;
        frameRateIntervalFrameCount_ = 0;
        initialized_ = false;
    }

    bool TimeSystem::IsInitialized() const noexcept
    {
        return initialized_;
    }

    void TimeSystem::Reset() noexcept
    {
        const Float32 maxDeltaSeconds = snapshot_.maxDeltaSeconds;
        const Float32 fixedDeltaSeconds = snapshot_.fixedDeltaSeconds;
        snapshot_ = TimeSnapshot{};
        snapshot_.maxDeltaSeconds = maxDeltaSeconds;
        snapshot_.fixedDeltaSeconds = fixedDeltaSeconds;
        frameRateIntervalElapsedSeconds_ = 0.0;
        frameRateIntervalFrameCount_ = 0;
        lastTickTime_ = Clock::now();
    }

    void TimeSystem::Tick() noexcept
    {
        const Clock::time_point now = Clock::now();
        const std::chrono::duration<Float32> delta = now - lastTickTime_;
        lastTickTime_ = now;
        AdvanceUnlocked(delta.count());
    }

    void TimeSystem::Advance(Float32 rawDeltaSeconds) noexcept
    {
        AdvanceUnlocked(rawDeltaSeconds);
    }

    void TimeSystem::AdvanceUnlocked(Float32 rawDeltaSeconds) noexcept
    {
        TimeSnapshot& snapshot = snapshot_;
        const Float32 rawDelta = ClampRawDelta(rawDeltaSeconds);
        const Float32 delta = std::min(rawDelta, snapshot.maxDeltaSeconds);

        snapshot.rawDeltaSeconds = rawDelta;
        snapshot.deltaSeconds = delta;
        snapshot.totalSeconds += static_cast<Float64>(delta);
        snapshot.fixedAccumulatorSeconds += static_cast<Float64>(delta);
        ++snapshot.frameIndex;

        snapshot.fixedStepCount = CalculateFixedStepCount(snapshot.fixedAccumulatorSeconds, snapshot.fixedDeltaSeconds);
        snapshot.fixedAccumulatorSeconds =
            std::max(0.0, snapshot.fixedAccumulatorSeconds - static_cast<Float64>(snapshot.fixedStepCount) * static_cast<Float64>(snapshot.fixedDeltaSeconds));

        FrameRateStats& frameRateStats = snapshot.frameRateStats;
        const Float32 currentFramesPerSecond = CalculateCurrentFrameRate(snapshot.deltaSeconds);
        frameRateStats.currentFramesPerSecond = currentFramesPerSecond;

        if (currentFramesPerSecond <= 0.0f)
        {
            return;
        }

        frameRateIntervalElapsedSeconds_ += static_cast<Float64>(snapshot.deltaSeconds);
        ++frameRateIntervalFrameCount_;

        if (frameRateIntervalElapsedSeconds_ >= 1.0)
        {
            frameRateStats.averageFramesPerSecond = static_cast<Float32>(static_cast<Float64>(frameRateIntervalFrameCount_) / frameRateIntervalElapsedSeconds_);
            frameRateIntervalElapsedSeconds_ = 0.0;
            frameRateIntervalFrameCount_ = 0;
        }
    }

    TimeSnapshot TimeSystem::GetSnapshot() const noexcept
    {
        return snapshot_;
    }

    UInt64 TimeSystem::GetFrameIndex() const noexcept
    {
        return snapshot_.frameIndex;
    }

    Float64 TimeSystem::GetTotalSeconds() const noexcept
    {
        return snapshot_.totalSeconds;
    }

    Float64 TimeSystem::GetFixedAccumulatorSeconds() const noexcept
    {
        return snapshot_.fixedAccumulatorSeconds;
    }

    Float32 TimeSystem::GetRawDeltaSeconds() const noexcept
    {
        return snapshot_.rawDeltaSeconds;
    }

    Float32 TimeSystem::GetDeltaSeconds() const noexcept
    {
        return snapshot_.deltaSeconds;
    }

    Float32 TimeSystem::GetMaxDeltaSeconds() const noexcept
    {
        return snapshot_.maxDeltaSeconds;
    }

    Float32 TimeSystem::GetFixedDeltaSeconds() const noexcept
    {
        return snapshot_.fixedDeltaSeconds;
    }

    UInt32 TimeSystem::GetFixedStepCount() const noexcept
    {
        return snapshot_.fixedStepCount;
    }

    FrameRateStats TimeSystem::GetFrameRateStats() const noexcept
    {
        return snapshot_.frameRateStats;
    }

    Float32 TimeSystem::GetCurrentFrameRate() const noexcept
    {
        return snapshot_.frameRateStats.currentFramesPerSecond;
    }

    Float32 TimeSystem::GetAverageFrameRate() const noexcept
    {
        return snapshot_.frameRateStats.averageFramesPerSecond;
    }

    bool TimeSystem::SetMaxDeltaSeconds(Float32 maxDeltaSeconds) noexcept
    {
        if (!IsPositiveFinite(maxDeltaSeconds))
        {
            return false;
        }

        snapshot_.maxDeltaSeconds = maxDeltaSeconds;
        return true;
    }

    bool TimeSystem::SetFixedDeltaSeconds(Float32 fixedDeltaSeconds) noexcept
    {
        if (!IsPositiveFinite(fixedDeltaSeconds))
        {
            return false;
        }

        snapshot_.fixedDeltaSeconds = fixedDeltaSeconds;
        snapshot_.fixedStepCount = 0;
        return true;
    }

    bool TimeSystem::HasFixedStep() const noexcept
    {
        return snapshot_.fixedStepCount > 0;
    }
} // namespace ve
