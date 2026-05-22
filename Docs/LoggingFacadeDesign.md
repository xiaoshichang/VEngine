# VEngine Logging Facade Design

## 1. Purpose

`Logging` provides the engine-wide logging facade used by runtime modules, tools, Player, and Editor code. The facade is
implemented on top of `Boost.Log`, but ordinary engine code should not include or call `Boost.Log` APIs directly.

The first-stage goal is to provide a practical, testable logging system with predictable initialization, useful default
sinks, source-location diagnostics, and clear extension points for the future Editor Console.

## 2. Design Decisions

The first-stage `Logging` design follows these decisions:

- `Logging` is a separate runtime module under `Engine/Runtime/Logging`.
- `Logging` may depend on `Core`.
- `Core` does not depend on `Logging`.
- The backend is `Boost.Log`.
- Public code uses the VEngine logging facade and `VE_LOG_*` macros.
- Logging initialization is explicit.
- Calling `InitializeLogging()` while logging is already initialized returns an error.
- `Fatal` is only the highest severity level in the first version; it does not abort or break by itself.
- Log formatting uses C++20 `std::format` and `{}` placeholders.
- Windows defaults to console, file, and debugger-output sinks.
- iOS defaults to console output; file output is available through explicit config.
- Editor Console support is exposed through a callback hook, not an Editor UI implementation.
- Assertion logging is opt-in through an explicit `InstallAssertionLogHandler()` function.
- Tests are registered through CMake/CTest.

## 3. Responsibilities

`Logging` owns:

- Log severity definitions.
- Logging configuration.
- Logging initialization and shutdown.
- VEngine logging macros.
- Source-location capture for log call sites.
- Message formatting.
- Console output sink.
- File output sink.
- Windows debugger-output sink.
- Editor callback hook.
- Optional assertion-handler integration.
- Logging tests.

## 4. Non-Responsibilities

`Logging` does not own:

- Crash handling.
- Fatal error termination policy.
- Assertion macro definitions.
- Editor Console UI.
- Platform-specific writable directory policy.
- FileSystem abstraction.
- Structured telemetry.
- Remote logging.
- Log viewer tooling.
- Log rotation beyond simple file truncation/opening behavior.

## 5. Dependency Rules

Allowed dependencies:

```text
C++20 standard library
Boost.Log
Boost.Core / Boost.System dependencies required by Boost.Log
Core
```

Forbidden dependencies for the first implementation:

```text
RHI
Platform module
Memory module
Threading module
Reflection module
Resource module
Scene module
Editor UI
Player code
Tools code
```

The first implementation may use small platform-specific preprocessor branches inside `Logging` when needed, such as
Windows debugger output, but it should not depend on a future Platform module.

## 6. Planned File Layout

```text
Engine/Runtime/Logging/
  Log.h
  Log.cpp
```

The first implementation should add only useful tracked files and should not create empty placeholder directories.

## 7. Public API Shape

Expected public types:

```cpp
namespace ve
{
enum class LogSeverity
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

struct LoggingConfig
{
    LogSeverity minimumSeverity;
    bool enableConsole;
    bool enableFile;
    bool enableDebuggerOutput;
    std::filesystem::path filePath;
};

struct LogRecord
{
    LogSeverity severity;
    const char* category;
    std::string_view message;
    SourceLocation location;
};

using LogCallback = void (*)(const LogRecord& record);
}
```

Expected public functions:

```cpp
namespace ve
{
[[nodiscard]] LoggingConfig MakeDefaultLoggingConfig();

[[nodiscard]] Result<void> InitializeLogging(const LoggingConfig& config = MakeDefaultLoggingConfig());
void ShutdownLogging() noexcept;
[[nodiscard]] bool IsLoggingInitialized() noexcept;

void SetLogCallback(LogCallback callback) noexcept;
[[nodiscard]] LogCallback GetLogCallback() noexcept;

void LogMessage(
    LogSeverity severity,
    const char* category,
    std::string_view message,
    SourceLocation location = SourceLocation::current());

[[nodiscard]] const char* ToString(LogSeverity severity) noexcept;

[[nodiscard]] Result<void> InstallAssertionLogHandler();
void UninstallAssertionLogHandler() noexcept;
}
```

Rules:

- `InitializeLogging()` must be called before normal logging.
- Calling `InitializeLogging()` when logging is already initialized returns `Result<void>::Failure` with
  `ErrorCode::InvalidState`.
- `ShutdownLogging()` is safe to call when logging is not initialized.
- `LogMessage()` should tolerate being called before initialization by using a minimal fallback path or by dropping the
  message without crashing. The first implementation should prefer a minimal fallback to standard error or debugger output
  so early failures are visible.
- `SetLogCallback(nullptr)` removes the callback.
- The callback is an extension point for Editor Console and tests. The callback must not be required for normal sinks.

## 8. Logging Macros

The facade exposes default-category macros and explicit-category macros.

Default category:

```cpp
VE_LOG_TRACE("Loading scene {}", sceneName);
VE_LOG_DEBUG("Frame {}", frameIndex);
VE_LOG_INFO("Engine started");
VE_LOG_WARN("Missing optional config {}", configPath);
VE_LOG_ERROR("Failed to load asset {}", assetPath);
VE_LOG_FATAL("Unrecoverable startup failure: {}", reason);
```

Explicit category:

```cpp
VE_LOG_TRACE_CATEGORY("Render", "Creating pipeline {}", pipelineName);
VE_LOG_DEBUG_CATEGORY("Resource", "Cache hit {}", assetId);
VE_LOG_INFO_CATEGORY("Script", "Loaded assembly {}", assemblyPath);
VE_LOG_WARN_CATEGORY("FileSystem", "Path does not exist {}", path);
VE_LOG_ERROR_CATEGORY("RHI", "Device creation failed: {}", reason);
VE_LOG_FATAL_CATEGORY("Application", "Startup failed: {}", reason);
```

Macro rules:

- Default category is `"General"`.
- Macro names use the `VE_` prefix.
- Macros capture `SourceLocation::current()` at the call site.
- Macros use `std::format` with `{}` placeholders.
- `VE_LOG_FATAL` logs only. It does not abort, throw, or break.

Implementation note:

- The first implementation must validate `std::format` support in the active Windows MSVC toolchain and in the intended
  iOS/Xcode toolchain before relying on it for iOS builds.
- If an intended first-stage compiler cannot support `std::format`, the project should explicitly revisit the decision and
  choose either a small independent `fmt` dependency or a restricted formatting fallback.

## 9. Severity Filtering

Default minimum severity:

```text
Debug builds:   Trace
Release builds: Info
```

Rules:

- Messages below `LoggingConfig::minimumSeverity` are ignored.
- Ignored messages should not reach sinks or callbacks.
- Formatting should ideally be avoided for messages that are filtered out. The first implementation can accomplish this
  with a lightweight `ShouldLog(LogSeverity severity)` check inside the macro helper.

## 10. Sinks

### 10.1 Console Sink

Console sink is enabled by default on Windows and iOS.

Expected behavior:

- Writes formatted single-line records.
- The current Boost.Log console sink writes to `std::clog`.
- The pre-initialization fallback writes `Trace`, `Debug`, and `Info` to `std::clog`, and `Warn`, `Error`, and `Fatal` to
  `std::cerr`.

### 10.2 File Sink

Windows default:

```text
Logs/VEngine.log
```

Rules:

- The path is relative to the current working directory unless `LoggingConfig::filePath` is absolute.
- The directory should be created with `std::filesystem::create_directories`.
- The first implementation truncates the configured file during initialization.
- iOS file logging is disabled by default because sandbox paths should later come from Platform/FileSystem.

### 10.3 Windows Debugger Output Sink

Windows default:

```text
enabled
```

Expected behavior:

- Writes to debugger output through a Windows-specific sink or small Windows-specific backend branch.
- Does not require a future Platform module.
- Non-Windows builds ignore this option.

### 10.4 Callback Sink

The callback sink supports Editor Console and tests.

Rules:

- Only one callback is required for the first version.
- Callback receives a `LogRecord`.
- Callback is invoked after severity filtering.
- Callback should be invoked regardless of whether console or file sinks are enabled.
- The logging system should avoid holding internal locks while calling user code if practical.

## 11. Log Format

Formatted sink output should be a single line:

```text
[2026-05-22 14:30:01.123][Info][Render][Thread 1234] message (File.cpp:42 Function)
```

Fields:

- Local timestamp with millisecond precision.
- Severity.
- Category.
- Thread id.
- Message.
- Source file.
- Source line.
- Source function.

The callback receives structured fields through `LogRecord`; it does not need to parse the formatted line.

## 12. Assertion Integration

`Core` owns assertion macros and the assertion handler. `Logging` may connect to it explicitly.

Expected functions:

```cpp
[[nodiscard]] Result<void> InstallAssertionLogHandler();
void UninstallAssertionLogHandler() noexcept;
```

Rules:

- `InitializeLogging()` does not automatically install the assertion log handler.
- `InstallAssertionLogHandler()` installs a handler that logs assertion failures through the logging facade.
- Installing the assertion log handler before logging is initialized should return `ErrorCode::InvalidState`.
- `UninstallAssertionLogHandler()` restores the assertion handler that was active before installation.
- Repeated install calls should return `ErrorCode::InvalidState` rather than stacking handlers.

## 13. Threading And Global State

The first implementation may use standard-library synchronization primitives because the Threading module does not exist
yet.

Guidelines:

- Protect global logging state with `std::mutex`.
- Avoid depending on the future Threading module.
- Keep callback state simple.
- Avoid static destruction surprises by making `ShutdownLogging()` responsible for removing sinks and clearing callback
  state where practical.

## 14. First Implementation Scope

The first implementation should do the following:

1. Add `Engine/Runtime/Logging/Log.h`.
2. Add `Engine/Runtime/Logging/Log.cpp`.
3. Add `LogSeverity`, `LoggingConfig`, `LogRecord`, and `LogCallback`.
4. Add explicit `InitializeLogging()` and `ShutdownLogging()`.
5. Return an error on repeated initialization.
6. Add default config creation.
7. Add console, file, and Windows debugger-output behavior.
8. Add callback hook.
9. Add `VE_LOG_*` and `VE_LOG_*_CATEGORY` macros with `std::format` formatting.
10. Add `InstallAssertionLogHandler()` and `UninstallAssertionLogHandler()`.
11. Update CMake target sources for `VEngine`.
12. Add a separate `VEngineLoggingTests` executable with logging coverage.

The first implementation should not add:

- Editor Console UI.
- Crash handling.
- Log rotation.
- Async logging queue.
- Remote logging.
- Structured JSON log output.
- Platform/FileSystem directory integration.
- A new formatting dependency.

## 15. Testing Strategy

Tests remain registered through CMake/CTest.

Recommended first tests:

```text
Logging Initialization
  - Default config initializes successfully.
  - Repeated InitializeLogging returns InvalidState.
  - ShutdownLogging is safe.

Logging Output
  - Log macros compile and can write a file.
  - The default file path can be overridden for tests.
  - Category text appears in output.
  - Formatted message text appears in output.

Severity Filtering
  - Messages below minimum severity are filtered out.
  - Messages at or above minimum severity reach callback/file.

Callback Sink
  - Callback receives severity, category, message, and source location.
  - Clearing callback stops callback delivery.

Assertion Integration
  - InstallAssertionLogHandler requires initialized logging.
  - Assertion failures can be routed into logging after explicit install.
  - UninstallAssertionLogHandler restores the previous assertion handler.
```

Testing notes:

- Use a test-specific log file path under the CMake build tree or a generated test directory.
- Clean up test log files where practical.
- Avoid relying on debugger-output sink for assertions.
- Keep tests deterministic by calling `ShutdownLogging()` before and after logging test groups.

## 16. Completion Criteria

`Logging` first implementation is complete when:

- `Engine/Runtime/Logging/Log.h` and `Log.cpp` exist and are part of `VEngine`.
- Ordinary project code can log through `VE_LOG_*` macros without including Boost.Log.
- Windows default logging writes to console, file, and debugger output.
- Callback sink works.
- Repeated initialization returns `ErrorCode::InvalidState`.
- Assertion logging is available through explicit installation.
- `windows-msvc-tests` configure, build, and CTest pass.
- Release preset builds successfully.
- The `std::format` choice is validated against the Windows MSVC toolchain and explicitly revisited if an intended
  iOS/Xcode toolchain does not support it.
