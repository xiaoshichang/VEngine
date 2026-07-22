# Editor Startup Project Option Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a cross-platform `--project <path>` Editor option that opens the requested project after Editor initialization, then require this option for project-based Editor smoke tests.

**Architecture:** A small Editor Core startup module parses UTF-8 arguments and applies the resulting option through the existing `Editor::OpenProject()` path. Windows only converts native UTF-16 arguments to UTF-8; macOS copies `argv`. Platform Editor applications own the parsed options until Editor initialization completes.

**Tech Stack:** C++20, Win32 `CommandLineToArgvW`, macOS `argv`, CMake, existing VEngine Editor and logging facades.

---

### Task 1: Establish the failing smoke check

**Files:**
- Read: `Build/windows-msvc-debug/Debug/Logs/VEngine.log`

- [ ] **Step 1: Launch the current Editor with the intended option**

Run from `Build/windows-msvc-debug/Debug`:

```powershell
$editorProcess = Start-Process -FilePath .\VEngineWinEditor.exe -ArgumentList '--project', 'D:\github-desktop\VEngine\DemoProject' -PassThru
```

Expected before implementation: the process reaches `Editor initialized.` but the log does not contain `Opened editor project`, proving that `--project` is currently ignored.

- [ ] **Step 2: Stop only the process created by Step 1**

```powershell
Stop-Process -Id $editorProcess.Id
```

Expected: the diagnostic Editor process exits without interacting with the project-selection UI.

### Task 2: Add shared Editor startup parsing and application

**Files:**
- Create: `Editor/Core/EditorStartup.h`
- Create: `Editor/Core/EditorStartup.cpp`
- Modify: `Editor/Core/Editor.h`
- Modify: `Editor/Core/Editor.cpp`
- Modify: `CMake/Targets/Applications/Common.cmake`

- [ ] **Step 1: Define the startup option contract**

Create `Editor/Core/EditorStartup.h`:

```cpp
#pragma once

#include <span>
#include <string>

namespace ve::editor
{
    class Editor;

    struct EditorStartupOptions
    {
        std::string startupProjectPath;
        std::string errorMessage;
    };

    [[nodiscard]] EditorStartupOptions ParseEditorStartupOptions(std::span<const std::string> arguments);
    void QueueEditorStartupOptions(Editor& editor, const EditorStartupOptions& options);
} // namespace ve::editor
```

- [ ] **Step 2: Implement first-valid-project parsing and startup application**

Create `Editor/Core/EditorStartup.cpp` with this behavior:

```cpp
#include "Editor/Core/EditorStartup.h"

#include "Editor/Core/Editor.h"
#include "Engine/Runtime/Logging/Log.h"

namespace ve::editor
{
    EditorStartupOptions ParseEditorStartupOptions(std::span<const std::string> arguments)
    {
        EditorStartupOptions options;
        for (size_t argumentIndex = 1; argumentIndex < arguments.size(); ++argumentIndex)
        {
            if (arguments[argumentIndex] != "--project")
            {
                continue;
            }

            if (argumentIndex + 1 >= arguments.size() || arguments[argumentIndex + 1].empty() || arguments[argumentIndex + 1].starts_with("--"))
            {
                options.errorMessage = "Editor command-line option --project requires a non-empty path argument.";
                return options;
            }

            options.startupProjectPath = arguments[argumentIndex + 1];
            return options;
        }
        return options;
    }

    void QueueEditorStartupOptions(Editor& editor, const EditorStartupOptions& options)
    {
        if (!options.errorMessage.empty())
        {
            VE_LOG_ERROR_CATEGORY("Editor", "{}", options.errorMessage);
            return;
        }
        if (!options.startupProjectPath.empty())
        {
            VE_LOG_INFO_CATEGORY("Editor", "Opening startup project: {}", options.startupProjectPath);
            editor.RequestOpenProject(options.startupProjectPath);
        }
    }
} // namespace ve::editor
```

- [ ] **Step 3: Defer opening until the Scene Thread is running**

Declare `Editor::RequestOpenProject(std::string projectPath)`, store the value in a `pendingProjectPath_` member, and consume it at the start of `Editor::Render()` before beginning the ImGui frame:

```cpp
void Editor::RequestOpenProject(std::string projectPath)
{
    if (!initialized_.load(std::memory_order_acquire) || projectPath.empty())
    {
        return;
    }
    pendingProjectPath_ = std::move(projectPath);
}

if (!pendingProjectPath_.empty())
{
    std::string projectPath = std::move(pendingProjectPath_);
    pendingProjectPath_.clear();
    OpenProject(std::move(projectPath));
}
```

Clear `pendingProjectPath_` during `Editor::UnInit()`. This is required because `OpenProject()` creates scene objects and must run from the registered Scene Thread, not from application initialization.

- [ ] **Step 4: Register the new common sources**

Add these entries beside `Editor/Core/Editor.h` in `VE_EDITOR_COMMON_SOURCES`:

```cmake
Editor/Core/EditorStartup.cpp
Editor/Core/EditorStartup.h
```

- [ ] **Step 5: Build to expose missing platform wiring**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: the common module compiles; the option is not used until Task 3.

### Task 3: Wire Windows and macOS Editor startup

**Files:**
- Modify: `Editor/Windows/main.cpp`
- Modify: `Editor/Windows/WindowsEditorApplication.h`
- Modify: `Editor/Windows/WindowsEditorApplication.cpp`
- Modify: `Editor/macOS/main.mm`
- Modify: `Editor/macOS/MacEditorApplication.h`
- Modify: `Editor/macOS/MacEditorApplication.cpp`

- [ ] **Step 1: Parse the complete Windows command line as UTF-8**

In `Editor/Windows/main.cpp`, include `Editor/Core/EditorStartup.h`, `<shellapi.h>`, `<string>`, `<string_view>`, and `<vector>`, then add these private helpers:

```cpp
namespace
{
    [[nodiscard]] std::string WideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        const int requiredLength = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (requiredLength <= 0)
        {
            return {};
        }
        std::string result(static_cast<size_t>(requiredLength), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), requiredLength, nullptr, nullptr) <= 0)
        {
            return {};
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> GetUtf8CommandLineArguments()
    {
        int argumentCount = 0;
        LPWSTR* nativeArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (nativeArguments == nullptr)
        {
            return {};
        }
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<size_t>(argumentCount));
        for (int argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
        {
            arguments.push_back(WideToUtf8(nativeArguments[argumentIndex]));
        }
        LocalFree(nativeArguments);
        return arguments;
    }
} // namespace
```

Before moving `initParam`, parse and pass the options:

```cpp
const std::vector<std::string> arguments = GetUtf8CommandLineArguments();
ve::editor::EditorStartupOptions startupOptions = ve::editor::ParseEditorStartupOptions(arguments);
ve::editor::WindowsEditorApplication application(std::move(initParam), std::move(startupOptions));
```

The UTF conversion helper must return an empty string when conversion fails; this makes a failed `--project` value follow the parser's malformed-option path.

- [ ] **Step 2: Store and apply Windows startup options**

Include `Editor/Core/EditorStartup.h` from `WindowsEditorApplication.h` and change the constructor to:

```cpp
WindowsEditorApplication(ve::ApplicationInitParam initParam, EditorStartupOptions startupOptions);
```

Add:

```cpp
EditorStartupOptions startupOptions_;
```

Define the constructor as:

```cpp
WindowsEditorApplication::WindowsEditorApplication(ve::ApplicationInitParam initParam, EditorStartupOptions startupOptions)
    : ve::Application(std::move(initParam))
    , startupOptions_(std::move(startupOptions))
{
}
```

After `PlaceWindowsEditorStartupWindows(...)`, call:

```cpp
QueueEditorStartupOptions(editor_, startupOptions_);
```

- [ ] **Step 3: Parse macOS `argv` through the same parser**

In `Editor/macOS/main.mm`, include `Editor/Core/EditorStartup.h`, `<string>`, and `<vector>`. Copy all `argc` arguments into `std::vector<std::string>`, treating a null entry as empty, and construct `MacEditorApplication` with parsed startup options:

```cpp
std::vector<std::string> arguments;
arguments.reserve(static_cast<size_t>(argc));
for (int argumentIndex = 0; argumentIndex < argc; ++argumentIndex)
{
    arguments.emplace_back(argv[argumentIndex] == nullptr ? "" : argv[argumentIndex]);
}
ve::editor::EditorStartupOptions startupOptions = ve::editor::ParseEditorStartupOptions(arguments);
ve::editor::MacEditorApplication application(std::move(initParam), std::move(startupOptions));
```

- [ ] **Step 4: Store and apply macOS startup options**

Include `Editor/Core/EditorStartup.h` from `MacEditorApplication.h`, change its constructor declaration, and add the member:

```cpp
MacEditorApplication(ve::ApplicationInitParam initParam, EditorStartupOptions startupOptions);

EditorStartupOptions startupOptions_;
```

Define the constructor as:

```cpp
MacEditorApplication::MacEditorApplication(ve::ApplicationInitParam initParam, EditorStartupOptions startupOptions)
    : ve::Application(std::move(initParam))
    , startupOptions_(std::move(startupOptions))
{
}
```

After `PlaceMacEditorStartupWindows(...)`, call:

```cpp
QueueEditorStartupOptions(editor_, startupOptions_);
```

- [ ] **Step 5: Build the Windows Editor**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: `VEngineWinEditor.exe` links successfully. macOS code is reviewed for type and API symmetry because it cannot be built in the Windows environment.

### Task 4: Document the required project-test launch method

**Files:**
- Modify: `AGENTS.md`
- Modify: `Docs/Superpowers/Specs/2026-07-22-editor-startup-project-design.md`

- [ ] **Step 1: Add the Editor smoke-test rule**

Under `## Build Commands` in `AGENTS.md`, add:

````markdown
For Editor smoke tests and rendering diagnostics that require a project, launch the Editor with the project path instead of interacting with the project-selection UI:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Do not automate project opening with mouse coordinates, simulated clicks, or other project-selection UI input. Use `--project <path>` so tests are deterministic and reproducible.
````

- [ ] **Step 2: Keep the approved design synchronized**

Confirm the design's Verification section contains the same no-simulated-click requirement and `--project <path>` convention.

### Task 5: Verify automatic project opening and regressions

**Files:**
- Read: `Build/windows-msvc-debug/Debug/Logs/VEngine.log`

- [ ] **Step 1: Launch directly into DemoProject**

Run the built executable with `--project` and retain the returned process ID. Do not click the UI:

```powershell
Start-Process -FilePath .\VEngineWinEditor.exe -ArgumentList '--project', 'D:\github-desktop\VEngine\DemoProject' -PassThru
```

Expected log sequence:

```text
Editor initialized.
Opening startup project: D:\github-desktop\VEngine\DemoProject
Opened editor project: D:\github-desktop\VEngine\DemoProject
```

- [ ] **Step 2: Verify malformed-option logging**

Launch a second isolated run with only `--project`, wait for initialization, and stop the returned process.

Expected log entry:

```text
Editor command-line option --project requires a non-empty path argument.
```

The process must remain alive on the project-selection view until explicitly stopped.

- [ ] **Step 3: Build Editor and Player and run the existing tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor VEngineWinPlayer
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: both applications build and all registered tests pass.

- [ ] **Step 4: Check only scoped diffs and whitespace**

Run:

```text
git diff --check
git diff -- AGENTS.md CMake/Targets/Applications/Common.cmake Editor/Core/EditorStartup.h Editor/Core/EditorStartup.cpp Editor/Windows/main.cpp Editor/Windows/WindowsEditorApplication.h Editor/Windows/WindowsEditorApplication.cpp Editor/macOS/main.mm Editor/macOS/MacEditorApplication.h Editor/macOS/MacEditorApplication.cpp Docs/Superpowers/Specs/2026-07-22-editor-startup-project-design.md
```

Expected: no whitespace errors and no unrelated files modified by this implementation.
