# Editor Play, Pause, and Step Controls Design

## Goal

Add Unity-style Play, Pause, and Step controls to the centered Editor toolbar. Play toggles to Stop while a runtime scene is active. Pause freezes runtime simulation without freezing Editor rendering or interaction. While paused, each Step click advances one complete runtime frame by `1/60` second and then remains paused.

## Current State

`ProjectEditingView` currently renders one centered Play or Stop button. `Editor` has `Editing` and `Playing` states and snapshots the editing scene before loading a runtime copy. `SceneSystem` advances `TimeSystem`, fixed updates, physics, regular updates, and rendering in that order on the Scene Thread.

`TimeSystem` already supports monotonic `Tick()` and deterministic `Advance(Float32)`, but it does not have pause or queued-step state.

## State Model

`EditorPlayState` gains a third state:

```text
Editing
   | Play
   v
Playing ---- Pause ----> Paused
   ^                       |
   +-------- Pause --------+
                           |
                           Step
                           |
                           +-- advance 1/60s, remain Paused

Playing / Paused ---- Stop ----> Editing
```

`Editor::IsPlaying()` returns true for both `Playing` and `Paused`. Existing guards that prevent opening projects, changing scenes, rebuilding, or editing runtime-owned assets therefore remain active while paused.

Starting Play always enters `Playing`. Stopping Play, closing a project, or shutting down the Editor clears pause and any unconsumed Step request. The next Play session always starts unpaused.

## Toolbar Behavior

The centered toolbar always displays three adjacent buttons:

| Editor state | First button | Pause button | Step button |
| --- | --- | --- | --- |
| Editing | Play; enabled according to `CanStartPlay()` | Disabled | Disabled |
| Playing | Stop | Enabled | Disabled |
| Paused | Stop | Enabled and visually active | Enabled |

Clicking Pause outside Play has no effect because the disabled button does not dispatch an Editor command. Clicking Pause while Playing enters `Paused`; clicking it again resumes `Playing`. Clicking Step is accepted only in `Paused`.

The three-button group is centered as a unit. The implementation uses existing Dear ImGui button and disabled-state patterns and does not add new icon assets.

## TimeSystem Design

`TimeSystem` owns simulation pause and pending-step state so pause semantics remain consistent for variable update, fixed update, scripting, and physics.

The public API adds operations equivalent to:

```cpp
void SetPaused(bool paused) noexcept;
[[nodiscard]] bool IsPaused() const noexcept;
[[nodiscard]] bool RequestStep(Float32 deltaSeconds = DefaultFixedDeltaSeconds) noexcept;
```

`RequestStep` returns false when time is not paused or when the requested delta is not positive and finite. Only one request is needed because the toolbar can generate at most one Step click between Scene Thread ticks. A new click after the next tick creates the next step.

Both `Tick()` and deterministic `Advance()` use the same controlled-advance path:

- Not paused: advance with the supplied or measured raw delta.
- Paused without a Step request: advance the engine frame with simulation delta `0`.
- Paused with a Step request: consume it once and advance with its explicit delta.

The monotonic clock baseline is refreshed on every `Tick()`, including paused ticks, so resuming cannot accumulate the wall-clock duration spent paused into one large delta.

The default Step duration is `DefaultFixedDeltaSeconds`, currently `1/60` second, approximately 16.66 ms. The fixed accumulator therefore budgets the expected fixed update and physics step while the regular update and late update receive the same explicit delta.

Paused zero-delta frames continue incrementing the engine time frame index so frame-oriented systems remain live. Total simulation time and the fixed accumulator do not advance. The status bar uses the existing average FPS value, which remains at the last valid running sample while paused.

## Editor Integration

`Editor` adds pause-aware queries and commands, including behavior equivalent to:

```cpp
[[nodiscard]] bool IsPaused() const noexcept;
[[nodiscard]] bool CanTogglePause() const noexcept;
[[nodiscard]] bool CanStepPlay() const noexcept;
void TogglePause();
void StepPlay();
```

These commands validate the Editor state before forwarding to `EngineRuntime::GetTimeSystem()`:

- `TogglePause()` only changes `Playing` and `Paused`.
- `StepPlay()` only requests a Step while `Paused`.
- Invalid calls log a warning and do not mutate time or scene state.

`StartPlay()` clears TimeSystem pause after the runtime scene is loaded successfully. `StopPlay()` clears pause and pending Step state after the editing scene is restored successfully. Project shutdown and Editor shutdown also clear the time control state defensively.

No Editor command reaches RHI directly, and SceneSystem remains free of Editor-specific pause branches. Its generic runtime update path is tightened so `Update` and `LateUpdate` are called only when `deltaSeconds` is positive. FixedUpdate and physics already require a positive fixed-step budget. A paused zero-delta frame therefore performs no simulation work, while a consumed Step produces one normal scheduled frame.

## Frame Data Flow

Toolbar input is handled during the Editor render callback on the Scene Thread. A Pause or Step click updates TimeSystem control state after the current frame's simulation has already run. On the next Scene Thread frame:

1. Editor frame-start callbacks run.
2. `TimeSystem::Tick()` resolves real-time, paused, or Step delta.
3. `SceneSystem` consumes `fixedStepCount` for FixedUpdate and physics.
4. When `deltaSeconds` is positive, `SceneSystem` calls runtime Update and LateUpdate with that value.
5. Scene View, Game View, and Editor UI render normally.

Each Step click therefore produces exactly one visible simulation advance on the following frame and leaves the system paused afterward.

## Error Handling

- A Pause command outside Play is rejected without state changes.
- A Step command outside `Paused` is rejected without state changes.
- A non-finite or non-positive Step delta is rejected by TimeSystem.
- Failed Start or Stop scene transitions do not leave Editor and TimeSystem states inconsistent.
- Resume and Stop discard any unconsumed Step request.

## Verification

Add focused unit coverage for the isolated `TimeSystem` behavior:

- Paused `Advance()` produces zero delta and does not increase total simulation time.
- One Step request advances exactly once with `1/60` second and remains paused.
- A following paused frame returns to zero delta.
- Resuming clears an unconsumed Step and advances normally.
- Invalid Step requests are rejected.

Do not add unit tests for Editor, SceneSystem, Dear ImGui, physics, or RHI integration. Verify those with an Editor smoke test launched using:

```text
VEngineWinEditor.exe --project D:\github-desktop\VEngine\DemoProject
```

The smoke test confirms the three-button layout, Play/Stop switching, Pause toggling, repeated one-click Step behavior, resume without a large time jump, Stop restoring the editing scene, and continued Scene/Game View rendering while paused.
