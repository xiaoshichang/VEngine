# Editor Play, Pause, and Step Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add centered Unity-style Play/Stop, Pause, and Step controls that can freeze runtime simulation and advance a paused scene by one 1/60-second frame per click.

**Architecture:** `TimeSystem` owns pause and one-shot Step state so variable update, fixed update, scripting, and physics share one controlled clock. `Editor` owns the Editing/Playing/Paused state machine and forwards valid toolbar commands, while `SceneSystem` generically skips Update/LateUpdate for zero-delta frames and continues rendering.

**Tech Stack:** C++20, Dear ImGui, CMake/CTest, existing VEngine TimeSystem and Editor scene snapshot flow.

---

### Task 1: Add deterministic pause and Step behavior to TimeSystem

**Files:**
- Create: `Tests/Unit/TimeTests.cpp`
- Create: `CMake/Targets/Tests/TimeTests.cmake`
- Modify: `CMake/Targets/Tests.cmake`
- Modify: `Engine/Runtime/Time/Time.h`
- Modify: `Engine/Runtime/Time/Time.cpp`

- [ ] **Step 1: Register a focused TimeSystem test executable**

Create `CMake/Targets/Tests/TimeTests.cmake`:

```cmake
include_guard(GLOBAL)

function(ve_add_time_tests)
    add_executable(VEngineTimeTests
        Tests/Unit/TimeTests.cpp
    )

    target_link_libraries(VEngineTimeTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEngineTimeTests)

    add_test(
        NAME VEngineTimeTests
        COMMAND $<TARGET_FILE:VEngineTimeTests>
    )
endfunction()
```

In `CMake/Targets/Tests.cmake`, include the new file after `MathTests.cmake` and call `ve_add_time_tests()` after `ve_add_math_tests()`.

- [ ] **Step 2: Write failing tests for pause, Step, resume, and invalid requests**

Create `Tests/Unit/TimeTests.cpp`:

```cpp
#include "Engine/Runtime/Time/Time.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }
        return condition;
    }

    bool ExpectNear(ve::Float64 actual, ve::Float64 expected, ve::Float64 tolerance, const char* message)
    {
        return Expect(std::abs(actual - expected) <= tolerance, message);
    }

    bool TestPausedTimeDoesNotAdvance()
    {
        ve::TimeSystem time;
        bool passed = Expect(time.Initialize({}) == ve::ErrorCode::None, "TimeSystem should initialize");
        time.SetPaused(true);
        time.Advance(0.1f);
        const ve::TimeSnapshot snapshot = time.GetSnapshot();
        passed &= Expect(time.IsPaused(), "TimeSystem should report paused");
        passed &= Expect(snapshot.frameIndex == 1, "Paused frames should still increment frameIndex");
        passed &= Expect(snapshot.deltaSeconds == 0.0f, "Paused frames should expose zero delta");
        passed &= Expect(snapshot.fixedStepCount == 0, "Paused frames should not budget fixed steps");
        passed &= Expect(snapshot.totalSeconds == 0.0, "Paused frames should not accumulate simulation time");
        return passed;
    }

    bool TestStepAdvancesExactlyOnce()
    {
        ve::TimeSystem time;
        bool passed = Expect(time.Initialize({}) == ve::ErrorCode::None, "TimeSystem should initialize");
        time.SetPaused(true);
        passed &= Expect(time.RequestStep(), "Paused TimeSystem should accept a default Step");
        time.Advance(0.25f);
        const ve::TimeSnapshot stepped = time.GetSnapshot();
        passed &= ExpectNear(stepped.deltaSeconds, ve::DefaultFixedDeltaSeconds, 1.0e-6, "Step should use the default fixed delta");
        passed &= ExpectNear(stepped.totalSeconds, ve::DefaultFixedDeltaSeconds, 1.0e-6, "Step should advance total time once");
        passed &= Expect(stepped.fixedStepCount == 1, "Default Step should budget one fixed update");
        passed &= Expect(time.IsPaused(), "Step should keep TimeSystem paused");

        time.Advance(0.25f);
        const ve::TimeSnapshot pausedAgain = time.GetSnapshot();
        passed &= Expect(pausedAgain.deltaSeconds == 0.0f, "Step request should be consumed once");
        passed &= ExpectNear(pausedAgain.totalSeconds, stepped.totalSeconds, 1.0e-6, "Following paused frame should not advance total time");
        return passed;
    }

    bool TestResumeClearsPendingStep()
    {
        ve::TimeSystem time;
        bool passed = Expect(time.Initialize({}) == ve::ErrorCode::None, "TimeSystem should initialize");
        time.SetPaused(true);
        passed &= Expect(time.RequestStep(), "Paused TimeSystem should accept Step before resume");
        time.SetPaused(false);
        time.Advance(0.02f);
        const ve::TimeSnapshot snapshot = time.GetSnapshot();
        passed &= Expect(!time.IsPaused(), "TimeSystem should resume");
        passed &= ExpectNear(snapshot.deltaSeconds, 0.02, 1.0e-6, "Resume should use the new frame delta instead of pending Step");
        passed &= ExpectNear(snapshot.totalSeconds, 0.02, 1.0e-6, "Resume should not add a cleared Step");
        return passed;
    }

    bool TestInvalidStepRequestsAreRejected()
    {
        ve::TimeSystem time;
        bool passed = Expect(time.Initialize({}) == ve::ErrorCode::None, "TimeSystem should initialize");
        passed &= Expect(!time.RequestStep(), "Running TimeSystem should reject Step");
        time.SetPaused(true);
        passed &= Expect(!time.RequestStep(0.0f), "Zero Step should be rejected");
        passed &= Expect(!time.RequestStep(-0.01f), "Negative Step should be rejected");
        passed &= Expect(!time.RequestStep(std::numeric_limits<ve::Float32>::quiet_NaN()), "Non-finite Step should be rejected");
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;
    passed &= TestPausedTimeDoesNotAdvance();
    passed &= TestStepAdvancesExactlyOnce();
    passed &= TestResumeClearsPendingStep();
    passed &= TestInvalidStepRequestsAreRejected();
    if (passed)
    {
        std::cout << "VEngineTimeTests passed\n";
        return 0;
    }
    return 1;
}
```

- [ ] **Step 3: Configure and build to verify the test fails before implementation**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineTimeTests
```

Expected: configure succeeds and compilation fails because `TimeSystem::SetPaused`, `IsPaused`, and `RequestStep` do not exist yet.

- [ ] **Step 4: Add the TimeSystem public API and state**

In `TimeSystem` public methods in `Engine/Runtime/Time/Time.h`, after `Advance`, add:

```cpp
void SetPaused(bool paused) noexcept;
[[nodiscard]] bool IsPaused() const noexcept;
[[nodiscard]] bool RequestStep(Float32 deltaSeconds = DefaultFixedDeltaSeconds) noexcept;
```

In private methods add:

```cpp
void AdvanceControlled(Float32 rawDeltaSeconds) noexcept;
```

Add these members after `snapshot_`:

```cpp
Float32 pendingStepSeconds_ = 0.0f;
bool paused_ = false;
```

- [ ] **Step 5: Implement controlled time advancement**

In `Engine/Runtime/Time/Time.cpp`, clear `paused_` and `pendingStepSeconds_` in `Initialize`, `Shutdown`, and `Reset`.

Change both `Tick()` and `Advance()` to call `AdvanceControlled` instead of `AdvanceUnlocked`, then add:

```cpp
void TimeSystem::SetPaused(bool paused) noexcept
{
    paused_ = paused;
    pendingStepSeconds_ = 0.0f;
    lastTickTime_ = Clock::now();
}

bool TimeSystem::IsPaused() const noexcept
{
    return paused_;
}

bool TimeSystem::RequestStep(Float32 deltaSeconds) noexcept
{
    if (!initialized_ || !paused_ || !IsPositiveFinite(deltaSeconds))
    {
        return false;
    }

    pendingStepSeconds_ = deltaSeconds;
    return true;
}

void TimeSystem::AdvanceControlled(Float32 rawDeltaSeconds) noexcept
{
    if (!paused_)
    {
        AdvanceUnlocked(rawDeltaSeconds);
        return;
    }

    const Float32 stepSeconds = pendingStepSeconds_;
    pendingStepSeconds_ = 0.0f;
    AdvanceUnlocked(stepSeconds);
}
```

`Tick()` must still update `lastTickTime_` before `AdvanceControlled`, ensuring paused wall time cannot accumulate.

- [ ] **Step 6: Build and run the focused test**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineTimeTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-debug -C Debug -R VEngineTimeTests --output-on-failure
```

Expected: `VEngineTimeTests` builds and reports `100% tests passed`.

- [ ] **Step 7: Commit TimeSystem behavior and its focused tests**

```powershell
git add -- CMake/Targets/Tests.cmake CMake/Targets/Tests/TimeTests.cmake Tests/Unit/TimeTests.cpp Engine/Runtime/Time/Time.h Engine/Runtime/Time/Time.cpp
git commit -m "engine: add paused time stepping"
```

### Task 2: Prevent zero-delta runtime script updates

**Files:**
- Modify: `Engine/Runtime/Scene/SceneSystem.cpp:87`

- [ ] **Step 1: Add the generic positive-delta guard**

Change `UpdateScene` to:

```cpp
void UpdateScene(SceneSystemImpl& impl, Float32 deltaSeconds)
{
    if (impl.scene != nullptr && impl.scene->GetExecutionMode() == SceneExecutionMode::Runtime && deltaSeconds > 0.0f)
    {
        impl.scene->Update(deltaSeconds);
        impl.scene->LateUpdate(deltaSeconds);
    }
}
```

This is a generic simulation rule, not an Editor-specific branch. FixedUpdate and physics already require a nonzero `fixedStepCount`.

- [ ] **Step 2: Build the engine and run TimeSystem tests**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngine VEngineTimeTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-debug -C Debug -R VEngineTimeTests --output-on-failure
```

Expected: both targets build and the focused tests pass.

- [ ] **Step 3: Commit the zero-delta scheduling rule**

```powershell
git add -- Engine/Runtime/Scene/SceneSystem.cpp
git commit -m "engine: skip zero-delta scene updates"
```

### Task 3: Add Pause and Step commands to the Editor state machine

**Files:**
- Modify: `Editor/Core/Editor.h:76`
- Modify: `Editor/Core/Editor.cpp:713`

- [ ] **Step 1: Extend Editor queries, commands, and state**

In `Editor/Core/Editor.h`, add these public methods after `IsPlaying()`:

```cpp
[[nodiscard]] bool IsPaused() const noexcept;
[[nodiscard]] bool CanTogglePause() const noexcept;
[[nodiscard]] bool CanStepPlay() const noexcept;
void TogglePause();
void StepPlay();
```

Change the private play-state enum to:

```cpp
enum class EditorPlayState
{
    Editing,
    Playing,
    Paused,
};
```

- [ ] **Step 2: Implement pause-aware state queries and commands**

In `Editor/Core/Editor.cpp`, change and add the query methods:

```cpp
bool Editor::IsPlaying() const noexcept
{
    return playState_ != EditorPlayState::Editing;
}

bool Editor::IsPaused() const noexcept
{
    return playState_ == EditorPlayState::Paused;
}

bool Editor::CanTogglePause() const noexcept
{
    return IsPlaying() && runtime_ != nullptr;
}

bool Editor::CanStepPlay() const noexcept
{
    return IsPaused() && runtime_ != nullptr;
}
```

Keep `CanStartPlay()` unchanged and change `CanStopPlay()` to use `IsPlaying()` instead of comparing only with `Playing`.

Add:

```cpp
void Editor::TogglePause()
{
    if (!CanTogglePause())
    {
        VE_LOG_WARN_CATEGORY("Editor", "Skipped Pause because Play mode is not active.");
        return;
    }

    VE_ASSERT(runtime_ != nullptr);
    const bool shouldPause = playState_ == EditorPlayState::Playing;
    runtime_->GetTimeSystem().SetPaused(shouldPause);
    playState_ = shouldPause ? EditorPlayState::Paused : EditorPlayState::Playing;
    VE_LOG_INFO_CATEGORY("Editor", shouldPause ? "Paused Play mode." : "Resumed Play mode.");
}

void Editor::StepPlay()
{
    if (!CanStepPlay())
    {
        VE_LOG_WARN_CATEGORY("Editor", "Skipped Step because Play mode is not paused.");
        return;
    }

    VE_ASSERT(runtime_ != nullptr);
    if (!runtime_->GetTimeSystem().RequestStep())
    {
        VE_LOG_WARN_CATEGORY("Editor", "Failed to queue a paused Play step.");
    }
}
```

- [ ] **Step 3: Reset time control at every Play lifecycle boundary**

After a successful runtime scene load in `StartPlay()`, call `runtime_->GetTimeSystem().SetPaused(false)` before setting `playState_` to `Playing`.

After a successful editing-scene restore in `StopPlay()`, call `runtime_->GetTimeSystem().SetPaused(false)` before clearing the snapshot and setting `Editing`.

At the beginning of `ShutdownOpenProjectState()`, and near the beginning of `UnInit()` before `runtime_` is cleared, add:

```cpp
if (runtime_ != nullptr)
{
    runtime_->GetTimeSystem().SetPaused(false);
}
```

- [ ] **Step 4: Build the Editor and run focused tests**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor VEngineTimeTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-debug -C Debug -R VEngineTimeTests --output-on-failure
```

Expected: Editor and Time tests build; focused tests pass.

- [ ] **Step 5: Commit the Editor state machine**

```powershell
git add -- Editor/Core/Editor.h Editor/Core/Editor.cpp
git commit -m "editor: add pause and step play states"
```

### Task 4: Render the centered Play, Pause, and Step toolbar

**Files:**
- Modify: `Editor/Core/EditorProjectEditingView.cpp:208`

- [ ] **Step 1: Replace the single-button layout with a centered three-button group**

In `ProjectEditingView::RenderToolbar`, use:

```cpp
constexpr float ButtonWidth = 64.0F;
const float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
const float groupWidth = ButtonWidth * 3.0F + buttonSpacing * 2.0F;
const float buttonX = (std::max)(0.0F, (ImGui::GetContentRegionAvail().x - groupWidth) * 0.5F);
ImGui::SetCursorPosX(ImGui::GetCursorPosX() + buttonX);
```

Render the first button with:

```cpp
if (editor.IsPlaying())
{
    if (ImGui::Button("Stop", ImVec2(ButtonWidth, 0.0F)))
    {
        editor.StopPlay();
    }
}
else
{
    const bool canStartPlay = editor.CanStartPlay();
    if (!canStartPlay)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Play", ImVec2(ButtonWidth, 0.0F)))
    {
        editor.StartPlay();
    }
    if (!canStartPlay)
    {
        ImGui::EndDisabled();
    }
}
```

Then render Pause and Step:

```cpp
ImGui::SameLine();
const bool canTogglePause = editor.CanTogglePause();
const bool isPaused = editor.IsPaused();
if (!canTogglePause)
{
    ImGui::BeginDisabled();
}
if (isPaused)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
}
if (ImGui::Button("Pause", ImVec2(ButtonWidth, 0.0F)))
{
    editor.TogglePause();
}
if (isPaused)
{
    ImGui::PopStyleColor();
}
if (!canTogglePause)
{
    ImGui::EndDisabled();
}

ImGui::SameLine();
const bool canStepPlay = editor.CanStepPlay();
if (!canStepPlay)
{
    ImGui::BeginDisabled();
}
if (ImGui::Button("Step", ImVec2(ButtonWidth, 0.0F)))
{
    editor.StepPlay();
}
if (!canStepPlay)
{
    ImGui::EndDisabled();
}
```

- [ ] **Step 2: Build the Editor**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: build succeeds without Dear ImGui stack or API errors.

- [ ] **Step 3: Commit the toolbar UI**

```powershell
git add -- Editor/Core/EditorProjectEditingView.cpp
git commit -m "editor: add play pause step toolbar"
```

### Task 5: Verify the complete interaction and clean the workspace

**Files:**
- Verify only: `DemoProject/Assets/Scenes/SampleScene.vescene`

- [ ] **Step 1: Run the complete Windows test suite**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: all registered tests, including `VEngineTimeTests`, pass with zero failures.

- [ ] **Step 2: Launch the project without simulating project-selection clicks**

Run:

```powershell
$editorProcess = Start-Process -FilePath 'D:\github-desktop\VEngine\Build\windows-msvc-debug\Debug\VEngineWinEditor.exe' -ArgumentList '--project','D:\github-desktop\VEngine\DemoProject' -PassThru
$editorProcess.Id
```

Expected: the DemoProject opens directly and the centered toolbar shows Play, disabled Pause, and disabled Step.

- [ ] **Step 3: Exercise Play, Pause, repeated Step, resume, and Stop**

Using the visible toolbar after direct project launch:

1. Click Play and confirm it changes to Stop while Pause becomes enabled and Step remains disabled.
2. Click Pause and confirm it appears active, Step becomes enabled, and falling objects stop changing position while both viewports continue rendering.
3. Click Step once and confirm the runtime scene advances once and remains paused.
4. Click Step several more times and confirm each click produces one additional small advance.
5. Click Pause again and confirm continuous simulation resumes without a large catch-up jump.
6. Click Stop and confirm the editing scene snapshot is restored and Pause/Step return to disabled.

Inspect `Build/windows-msvc-debug/Debug/Logs/VEngine.log`. Expected: Play, Pause, resume, and Stop transitions are logged; there are no assertion, shader, pipeline, scripting, or physics errors.

Stop only the exact `$editorProcess.Id` created in Step 2.

- [ ] **Step 4: Remove generated smoke artifacts and check scope**

Remove only smoke-generated screenshots and a root `imgui.ini` if it did not exist before the smoke test. Then run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors, no smoke artifacts, and only the intended source/test changes or commits.
