#include "Engine/Runtime/Time/Time.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace ve::Time
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        struct TimeState
        {
            Clock::time_point lastTickTime = Clock::now();
            TimeSnapshot snapshot;
        };

        TimeState gTimeState;

        [[nodiscard]] bool IsPositiveFinite(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] float ClampRawDelta(float rawDeltaSeconds) noexcept
        {
            if (!std::isfinite(rawDeltaSeconds) || rawDeltaSeconds < 0.0f)
            {
                return 0.0f;
            }

            return rawDeltaSeconds;
        }

        [[nodiscard]] uint32_t CalculateFixedStepCount(double accumulatorSeconds, float fixedDeltaSeconds) noexcept
        {
            if (fixedDeltaSeconds <= 0.0f)
            {
                return 0;
            }

            const double pendingSteps = accumulatorSeconds / static_cast<double>(fixedDeltaSeconds);

            if (pendingSteps <= 0.0)
            {
                return 0;
            }

            constexpr double MaxCount = static_cast<double>(std::numeric_limits<uint32_t>::max());
            return static_cast<uint32_t>(std::min(std::floor(pendingSteps), MaxCount));
        }
    } // namespace

    void Reset() noexcept
    {
        gTimeState = TimeState{};
        gTimeState.lastTickTime = Clock::now();
        gTimeState.snapshot.maxDeltaSeconds = DefaultMaxDeltaSeconds;
        gTimeState.snapshot.fixedDeltaSeconds = DefaultFixedDeltaSeconds;
    }

    void Tick() noexcept
    {
        const Clock::time_point now = Clock::now();
        const std::chrono::duration<float> delta = now - gTimeState.lastTickTime;
        gTimeState.lastTickTime = now;
        Advance(delta.count());
    }

    void Advance(float rawDeltaSeconds) noexcept
    {
        TimeSnapshot& snapshot = gTimeState.snapshot;
        const float rawDelta = ClampRawDelta(rawDeltaSeconds);
        const float delta = std::min(rawDelta, snapshot.maxDeltaSeconds);

        snapshot.rawDeltaSeconds = rawDelta;
        snapshot.deltaSeconds = delta;
        snapshot.totalSeconds += static_cast<double>(delta);
        snapshot.fixedAccumulatorSeconds += static_cast<double>(delta);
        ++snapshot.frameIndex;

        snapshot.fixedStepCount = CalculateFixedStepCount(snapshot.fixedAccumulatorSeconds, snapshot.fixedDeltaSeconds);
        snapshot.fixedAccumulatorSeconds =
            std::max(0.0,
                     snapshot.fixedAccumulatorSeconds - static_cast<double>(snapshot.fixedStepCount) *
                                                            static_cast<double>(snapshot.fixedDeltaSeconds));
    }

    TimeSnapshot GetSnapshot() noexcept
    {
        return gTimeState.snapshot;
    }

    uint64_t GetFrameIndex() noexcept
    {
        return gTimeState.snapshot.frameIndex;
    }

    double GetTotalSeconds() noexcept
    {
        return gTimeState.snapshot.totalSeconds;
    }

    double GetFixedAccumulatorSeconds() noexcept
    {
        return gTimeState.snapshot.fixedAccumulatorSeconds;
    }

    float GetRawDeltaSeconds() noexcept
    {
        return gTimeState.snapshot.rawDeltaSeconds;
    }

    float GetDeltaSeconds() noexcept
    {
        return gTimeState.snapshot.deltaSeconds;
    }

    float GetMaxDeltaSeconds() noexcept
    {
        return gTimeState.snapshot.maxDeltaSeconds;
    }

    float GetFixedDeltaSeconds() noexcept
    {
        return gTimeState.snapshot.fixedDeltaSeconds;
    }

    uint32_t GetFixedStepCount() noexcept
    {
        return gTimeState.snapshot.fixedStepCount;
    }

    bool SetMaxDeltaSeconds(float maxDeltaSeconds) noexcept
    {
        if (!IsPositiveFinite(maxDeltaSeconds))
        {
            return false;
        }

        gTimeState.snapshot.maxDeltaSeconds = maxDeltaSeconds;
        return true;
    }

    bool SetFixedDeltaSeconds(float fixedDeltaSeconds) noexcept
    {
        if (!IsPositiveFinite(fixedDeltaSeconds))
        {
            return false;
        }

        gTimeState.snapshot.fixedDeltaSeconds = fixedDeltaSeconds;
        gTimeState.snapshot.fixedStepCount = 0;
        return true;
    }

    bool HasFixedStep() noexcept
    {
        return GetFixedStepCount() > 0;
    }
} // namespace ve::Time
