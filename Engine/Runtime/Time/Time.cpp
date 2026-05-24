#include "Engine/Runtime/Time/Time.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>

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
        std::mutex gTimeMutex;

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

        void AdvanceLocked(TimeState& state, float rawDeltaSeconds) noexcept
        {
            TimeSnapshot& snapshot = state.snapshot;
            const float rawDelta = ClampRawDelta(rawDeltaSeconds);
            const float delta = std::min(rawDelta, snapshot.maxDeltaSeconds);

            snapshot.rawDeltaSeconds = rawDelta;
            snapshot.deltaSeconds = delta;
            snapshot.totalSeconds += static_cast<double>(delta);
            snapshot.fixedAccumulatorSeconds += static_cast<double>(delta);
            ++snapshot.frameIndex;

            snapshot.fixedStepCount =
                CalculateFixedStepCount(snapshot.fixedAccumulatorSeconds, snapshot.fixedDeltaSeconds);
            snapshot.fixedAccumulatorSeconds =
                std::max(0.0,
                         snapshot.fixedAccumulatorSeconds - static_cast<double>(snapshot.fixedStepCount) *
                                                                static_cast<double>(snapshot.fixedDeltaSeconds));
        }
    } // namespace

    void Reset() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        gTimeState = TimeState{};
        gTimeState.lastTickTime = Clock::now();
        gTimeState.snapshot.maxDeltaSeconds = DefaultMaxDeltaSeconds;
        gTimeState.snapshot.fixedDeltaSeconds = DefaultFixedDeltaSeconds;
    }

    void Tick() noexcept
    {
        const Clock::time_point now = Clock::now();
        std::lock_guard<std::mutex> lock(gTimeMutex);
        const std::chrono::duration<float> delta = now - gTimeState.lastTickTime;
        gTimeState.lastTickTime = now;
        AdvanceLocked(gTimeState, delta.count());
    }

    void Advance(float rawDeltaSeconds) noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        AdvanceLocked(gTimeState, rawDeltaSeconds);
    }

    TimeSnapshot GetSnapshot() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot;
    }

    uint64_t GetFrameIndex() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot.frameIndex;
    }

    double GetTotalSeconds() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot.totalSeconds;
    }

    double GetFixedAccumulatorSeconds() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot.fixedAccumulatorSeconds;
    }

    float GetRawDeltaSeconds() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot.rawDeltaSeconds;
    }

    float GetDeltaSeconds() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot.deltaSeconds;
    }

    float GetMaxDeltaSeconds() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot.maxDeltaSeconds;
    }

    float GetFixedDeltaSeconds() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot.fixedDeltaSeconds;
    }

    uint32_t GetFixedStepCount() noexcept
    {
        std::lock_guard<std::mutex> lock(gTimeMutex);
        return gTimeState.snapshot.fixedStepCount;
    }

    bool SetMaxDeltaSeconds(float maxDeltaSeconds) noexcept
    {
        if (!IsPositiveFinite(maxDeltaSeconds))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(gTimeMutex);
        gTimeState.snapshot.maxDeltaSeconds = maxDeltaSeconds;
        return true;
    }

    bool SetFixedDeltaSeconds(float fixedDeltaSeconds) noexcept
    {
        if (!IsPositiveFinite(fixedDeltaSeconds))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(gTimeMutex);
        gTimeState.snapshot.fixedDeltaSeconds = fixedDeltaSeconds;
        gTimeState.snapshot.fixedStepCount = 0;
        return true;
    }

    bool HasFixedStep() noexcept
    {
        return GetFixedStepCount() > 0;
    }
} // namespace ve::Time
