#pragma once

#include <cstdint>

namespace ve::Time
{
constexpr float DefaultMaxDeltaSeconds = 0.25f;
constexpr float DefaultFixedDeltaSeconds = 1.0f / 60.0f;

struct TimeSnapshot
{
    /// Monotonically increasing engine frame index. Starts at 0 after Reset() and increments once per Tick() or Advance().
    uint64_t frameIndex = 0;

    /// Total clamped engine time in seconds since the last Reset(). This accumulates deltaSeconds, not rawDeltaSeconds.
    double totalSeconds = 0.0;

    /// Fractional fixed-step time carried into the next frame after fixedStepCount has been budgeted for this frame.
    double fixedAccumulatorSeconds = 0.0;

    /// Unclamped measured frame delta in seconds. This is useful for diagnostics and may be larger than deltaSeconds.
    float rawDeltaSeconds = 0.0f;

    /// Runtime frame delta in seconds after maxDeltaSeconds clamping. General update code should usually consume this.
    float deltaSeconds = 0.0f;

    /// Maximum allowed deltaSeconds value. Large raw deltas from stalls or breakpoints are clamped to this value.
    float maxDeltaSeconds = DefaultMaxDeltaSeconds;

    /// Target fixed-step duration in seconds. fixedStepCount is calculated from this value and the fixed accumulator.
    float fixedDeltaSeconds = DefaultFixedDeltaSeconds;

    /// Number of fixed steps budgeted for the current frame. Time does not execute these steps; a scheduler should consume it.
    uint32_t fixedStepCount = 0;
};

void Reset() noexcept;
void Tick() noexcept;
void Advance(float rawDeltaSeconds) noexcept;

[[nodiscard]] TimeSnapshot GetSnapshot() noexcept;
[[nodiscard]] uint64_t GetFrameIndex() noexcept;
[[nodiscard]] double GetTotalSeconds() noexcept;
[[nodiscard]] double GetFixedAccumulatorSeconds() noexcept;
[[nodiscard]] float GetRawDeltaSeconds() noexcept;
[[nodiscard]] float GetDeltaSeconds() noexcept;
[[nodiscard]] float GetMaxDeltaSeconds() noexcept;
[[nodiscard]] float GetFixedDeltaSeconds() noexcept;
[[nodiscard]] uint32_t GetFixedStepCount() noexcept;

[[nodiscard]] bool SetMaxDeltaSeconds(float maxDeltaSeconds) noexcept;
[[nodiscard]] bool SetFixedDeltaSeconds(float fixedDeltaSeconds) noexcept;
[[nodiscard]] bool HasFixedStep() noexcept;
}
