# VEngine Time System Design

## 1. Purpose

This document records the current first-stage design for VEngine's basic time system.

The goal is to provide a small global engine time module that can be consumed by early runtime systems, tests, and future
scene/script/update code. It is intentionally closer to an application clock than to a complete gameplay world time model.

The current design is not a full Unreal-style tick framework. It borrows the useful early idea of a globally accessible
engine/app time source, while leaving world time, pause, time scale, tick groups, and fixed-step execution ownership for
later systems.

## 2. Current Scope

The Time module lives under:

```text
Engine/Runtime/Time/Time.h
Engine/Runtime/Time/Time.cpp
```

It exposes global functions under `ve::Time`.

Current responsibilities:

- Track frame index.
- Track total engine time in seconds.
- Track raw frame delta from a monotonic clock.
- Track clamped frame delta for runtime update use.
- Provide configurable maximum delta clamp.
- Provide fixed-step configuration and per-frame fixed-step budget.
- Provide a snapshot API so consumers can copy stable frame time data.

Current non-responsibilities:

- It does not own Scene or World update.
- It does not execute fixed updates.
- It does not track pause.
- It does not track time scale.
- It does not implement tick groups.
- It does not schedule Actor, Component, Physics, Script, or Render work.
- It does not provide separate editor/game/simulation world clocks yet.

## 3. API Shape

The primary API is:

```cpp
namespace ve::Time
{
    void Reset() noexcept;
    void Tick() noexcept;
    void Advance(float rawDeltaSeconds) noexcept;

    TimeSnapshot GetSnapshot() noexcept;
    uint64_t GetFrameIndex() noexcept;
    double GetTotalSeconds() noexcept;
    double GetFixedAccumulatorSeconds() noexcept;
    float GetRawDeltaSeconds() noexcept;
    float GetDeltaSeconds() noexcept;
    float GetMaxDeltaSeconds() noexcept;
    float GetFixedDeltaSeconds() noexcept;
    uint32_t GetFixedStepCount() noexcept;

    bool SetMaxDeltaSeconds(float maxDeltaSeconds) noexcept;
    bool SetFixedDeltaSeconds(float fixedDeltaSeconds) noexcept;
    bool HasFixedStep() noexcept;
}
```

`Tick()` uses `std::chrono::steady_clock`, which is a cross-platform monotonic clock suitable for Windows and iOS.

`Advance()` exists for deterministic tests and future controlled simulation paths. It applies the same clamp and
accumulator behavior as `Tick()`, but accepts an explicit raw delta.

## 4. Time Snapshot

`TimeSnapshot` stores the current copied state:

```cpp
struct TimeSnapshot
{
    uint64_t frameIndex;
    double totalSeconds;
    double fixedAccumulatorSeconds;
    float rawDeltaSeconds;
    float deltaSeconds;
    float maxDeltaSeconds;
    float fixedDeltaSeconds;
    uint32_t fixedStepCount;
};
```

Type choices:

- `frameIndex` uses `uint64_t` so it remains stable for long runs.
- `totalSeconds` uses `double` to reduce long-running precision loss.
- `deltaSeconds` uses `float` because most gameplay and rendering code consumes frame deltas as float values.
- `rawDeltaSeconds` preserves the unclamped measured delta for diagnostics.
- `deltaSeconds` is the clamped delta intended for first-stage runtime update use.

## 5. Delta Clamp

The default max delta is:

```cpp
DefaultMaxDeltaSeconds = 0.25f;
```

Frame delta behavior:

```text
rawDeltaSeconds = measured monotonic frame delta
deltaSeconds = min(max(rawDeltaSeconds, 0), maxDeltaSeconds)
totalSeconds += deltaSeconds
```

The clamp prevents a breakpoint, window stall, or system hitch from producing a very large gameplay delta on the next
frame. The raw value remains available for diagnostics.

Invalid raw deltas such as negative, infinity, or NaN are treated as zero.

## 6. Fixed Step Budget

The Time module currently provides a fixed-step budget, not fixed-step execution.

The default fixed delta is:

```cpp
DefaultFixedDeltaSeconds = 1.0f / 60.0f;
```

Each `Tick()` or `Advance()` does:

```text
fixedAccumulatorSeconds += deltaSeconds
fixedStepCount = floor(fixedAccumulatorSeconds / fixedDeltaSeconds)
fixedAccumulatorSeconds -= fixedStepCount * fixedDeltaSeconds
```

This means:

- `fixedStepCount` is the number of fixed steps that a future scheduler may run for this frame.
- `fixedAccumulatorSeconds` stores only the fractional remainder after budgeting steps.
- The Time module does not expose `ConsumeFixedStep()` and does not own fixed-step execution state.

This is intentionally a small design adjustment: fixed-step execution should belong to a future Tick Scheduler,
Physics Scheduler, or Engine Loop stage. Time only provides the per-frame budget.

## 7. Game Thread Integration

`GameThreadSystem` owns frame time advancement because it owns world update. The current first-stage loop is:

```text
GameThreadSystem::Initialize()
    Time::Reset()

while running:
    BeginFrame
        Time::Tick()
    Update / LateUpdate / TransformUpdate
    RenderExtraction
    EndFrame
```

The Main Thread still pumps platform messages and publishes future input/lifecycle data, but it no longer calls
`Time::Tick()` each loop iteration. Game systems should consume the stable Time snapshot after the Game Thread enters the
frame. Non-game threads may read the snapshot for diagnostics or presentation, but they should not advance it.

This is only a first-stage loop. Later milestones should introduce a clearer Engine Loop with named phases.

## 8. Relationship To Unreal-Style Design

The current module is only loosely inspired by Unreal's broad shape.

Similarities:

- A global engine/app-level time source exists.
- Frame delta is updated once per engine frame.
- Runtime systems can consume a stable delta for update work.

Differences:

- VEngine does not yet have a World-level time object.
- VEngine does not yet have pause, time dilation, or per-world time scale.
- VEngine does not yet have tick groups or prerequisites.
- VEngine does not yet have a custom timestep policy object.
- VEngine Time does not execute fixed updates.

Recommended future layering:

```text
ve::Time
  Global engine/app monotonic time and frame delta.

EngineLoop / FrameClock
  Owns frame phases, custom timestep policy, and per-frame scheduling decisions.

WorldTime / SceneTime
  Owns gameplay pause, time scale, world delta, and editor/game simulation differences.

TickScheduler / PhysicsScheduler
  Consumes fixedStepCount and runs fixed update phases.
```

## 9. Testing

The current tests live in:

```text
Tests/Unit/TimeTests.cpp
```

The CMake target is:

```text
VEngineTimeTests
```

Covered behavior:

- Reset default state.
- Manual `Advance()` frame progression.
- Delta clamp.
- Invalid delta configuration rejection.
- Fixed-step budget and accumulator remainder.
- `Tick()` monotonic clock smoke test.

## 10. Future Iteration Notes

Likely future changes:

- Add a named Engine Loop phase around time update.
- Add World or Scene time separate from global engine time.
- Add pause and time scale at the World/Scene layer.
- Move custom timestep policy out of the Time module if needed.
- Have TickScheduler or PhysicsScheduler consume `fixedStepCount`.
- Add platform timer facade only if `std::chrono::steady_clock` proves insufficient.
