#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/Atomic.h"

#include <chrono>

namespace ve
{
    constexpr Float32 DefaultMaxDeltaSeconds = 0.25f;
    constexpr Float32 DefaultFixedDeltaSeconds = 1.0f / 60.0f;

    /// Describes initial TimeSystem configuration.
    struct TimeSystemInitParam
    {
        /// Maximum allowed deltaSeconds value. Large raw deltas from stalls or breakpoints are clamped to this value.
        Float32 maxDeltaSeconds = DefaultMaxDeltaSeconds;

        /// Target fixed-step duration in seconds. fixedStepCount is calculated from this value and the fixed
        /// accumulator.
        Float32 fixedDeltaSeconds = DefaultFixedDeltaSeconds;
    };

    /// Stores frame rate statistics derived from clamped runtime deltaSeconds values.
    struct FrameRateStats
    {
        /// FPS computed from the current frame's clamped deltaSeconds. Zero when the current delta sample is invalid
        /// or non-positive.
        Float32 currentFramesPerSecond = 0.0f;

        /// Average FPS updated once per second from frames counted in the latest one-second interval.
        Float32 averageFramesPerSecond = 0.0f;
    };

    struct TimeSnapshot
    {
        /// Monotonically increasing engine frame index. Starts at 0 after Initialize() and increments once per Tick()
        /// or Advance().
        UInt64 frameIndex = 0;

        /// Total clamped engine time in seconds since the last Initialize(). This accumulates deltaSeconds, not
        /// rawDeltaSeconds.
        Float64 totalSeconds = 0.0;

        /// Fractional fixed-step time carried into the next frame after fixedStepCount has been budgeted for this
        /// frame.
        Float64 fixedAccumulatorSeconds = 0.0;

        /// Unclamped measured frame delta in seconds. This is useful for diagnostics and may be larger than
        /// deltaSeconds.
        Float32 rawDeltaSeconds = 0.0f;

        /// Runtime frame delta in seconds after maxDeltaSeconds clamping. General update code should usually consume
        /// this.
        Float32 deltaSeconds = 0.0f;

        /// Maximum allowed deltaSeconds value. Large raw deltas from stalls or breakpoints are clamped to this value.
        Float32 maxDeltaSeconds = DefaultMaxDeltaSeconds;

        /// Target fixed-step duration in seconds. fixedStepCount is calculated from this value and the fixed
        /// accumulator.
        Float32 fixedDeltaSeconds = DefaultFixedDeltaSeconds;

        /// Number of fixed steps budgeted for the current frame. TimeSystem does not execute these steps; a scheduler
        /// should consume it.
        UInt32 fixedStepCount = 0;

        /// Frame rate statistics derived from clamped runtime deltas.
        FrameRateStats frameRateStats;
    };

    /// Owns engine frame time state.
    ///
    /// EngineRuntime owns and initializes TimeSystem, while SceneSystem's Scene Thread advances it so Scene update and
    /// component update consume a SceneThread-driven clock.
    class TimeSystem : public NonMovable
    {
    public:
        TimeSystem();
        ~TimeSystem();

        /// Initializes time state and captures the monotonic clock baseline.
        [[nodiscard]] ErrorCode Initialize(const TimeSystemInitParam& initParam);

        /// Clears initialized state. Shutdown is a no-op when the system is not initialized.
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Resets accumulated time while preserving the current delta configuration.
        void Reset() noexcept;

        /// Advances time using the monotonic clock delta since the previous Tick() or Initialize().
        void Tick() noexcept;

        /// Advances time using an explicit raw frame delta value. Useful for deterministic tests and controlled
        /// simulation paths.
        void Advance(Float32 rawDeltaSeconds) noexcept;

        /// Pauses or resumes simulation time. Rendering frames continue while paused with a zero delta.
        void SetPaused(bool paused) noexcept;

        [[nodiscard]] bool IsPaused() const noexcept;

        /// Requests one paused simulation frame using the supplied delta. The request is consumed by the next Tick()
        /// or Advance(). Requests are rejected while running or when deltaSeconds is invalid.
        [[nodiscard]] bool RequestStep(Float32 deltaSeconds = DefaultFixedDeltaSeconds) noexcept;

        [[nodiscard]] TimeSnapshot GetSnapshot() const noexcept;
        [[nodiscard]] UInt64 GetFrameIndex() const noexcept;
        [[nodiscard]] Float64 GetTotalSeconds() const noexcept;
        [[nodiscard]] Float64 GetFixedAccumulatorSeconds() const noexcept;
        [[nodiscard]] Float32 GetRawDeltaSeconds() const noexcept;
        [[nodiscard]] Float32 GetDeltaSeconds() const noexcept;
        [[nodiscard]] Float32 GetMaxDeltaSeconds() const noexcept;
        [[nodiscard]] Float32 GetFixedDeltaSeconds() const noexcept;
        [[nodiscard]] UInt32 GetFixedStepCount() const noexcept;
        [[nodiscard]] FrameRateStats GetFrameRateStats() const noexcept;
        [[nodiscard]] Float32 GetCurrentFrameRate() const noexcept;
        [[nodiscard]] Float32 GetAverageFrameRate() const noexcept;

        [[nodiscard]] bool SetMaxDeltaSeconds(Float32 maxDeltaSeconds) noexcept;
        [[nodiscard]] bool SetFixedDeltaSeconds(Float32 fixedDeltaSeconds) noexcept;
        [[nodiscard]] bool HasFixedStep() const noexcept;

    private:
        using Clock = std::chrono::steady_clock;

        void AdvanceControlled(Float32 rawDeltaSeconds) noexcept;
        void AdvanceUnlocked(Float32 rawDeltaSeconds) noexcept;

        Clock::time_point lastTickTime_ = Clock::now();
        Float64 frameRateIntervalElapsedSeconds_ = 0.0;
        UInt32 frameRateIntervalFrameCount_ = 0;
        TimeSnapshot snapshot_;
        Atomic<Float32> pendingStepSeconds_{0.0f};
        AtomicBool paused_{false};
        bool initialized_ = false;
    };
} // namespace ve
