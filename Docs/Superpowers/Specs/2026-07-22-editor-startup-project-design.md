# Editor Startup Project Command-Line Design

## Goal

Allow VEngine Editor to open a specified project during startup so automated and manual rendering checks can skip the project-selection UI.

Example:

```text
VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Windows and macOS use the same `--project <path>` syntax. Launching without the option preserves the current project-selection behavior.

## Command-Line Parsing

Add a small Editor-owned startup-options parser rather than exposing `Editor` from either platform application.

The parser accepts UTF-8 arguments and returns an `EditorStartupOptions` value containing an optional startup project path plus an optional parse-error message. Platform entry points remain responsible only for obtaining native command-line arguments:

- Windows uses `CommandLineToArgvW`, converts every argument from UTF-16 to UTF-8, and passes the resulting argument list to the parser.
- macOS passes its UTF-8 `argv` values to the same parser.

The first `--project` option consumes the following argument as its path. A missing value, an empty value, or another `--` option where the path is required is reported as an invalid option and does not attempt to open a project. Later `--project` occurrences are ignored after the first valid value. Unrecognized options remain available to their existing platform-specific consumers; the startup parser ignores them.

## Startup Flow

Each Editor application receives the parsed startup options through its constructor and stores them until initialization completes.

Startup order:

1. Initialize the base `Application` and runtime systems.
2. Initialize `Editor` and its project-selection view.
3. Place the Editor windows using the existing platform behavior.
4. If a non-empty startup project path was supplied, call `Editor::OpenProject()` on the main thread.
5. Enter the normal application loop.

This reuses the existing project preparation, asset scan/import, script compilation, scene load, recent-project update, and editing-view transition. No second project-opening path is introduced.

## Failure Behavior and Logging

An invalid command-line shape such as `--project` without a value logs an Editor-category error before the application loop begins. The Editor remains open on the project-selection view.

If the supplied path does not describe a valid project, `Editor::OpenProject()` keeps its existing detailed validation and logging behavior. Its failure path leaves the Editor usable on the project-selection view instead of terminating the process.

The selected startup path is logged before opening so automated runs can correlate subsequent asset or scene errors with the requested project.

## Scope

Included:

- Shared UTF-8 parsing for the `--project <path>` option.
- Windows and macOS Editor entry-point integration.
- Automatic project opening after successful Editor initialization.
- Clear logging for malformed startup arguments.

Excluded:

- Automatically opening the most recent project when no option is present.
- Player command-line changes.
- Multiple simultaneous project paths.
- Changing project descriptor or asset-loading behavior.

## Verification

Because this feature is tied to the Editor application lifecycle, verification uses focused build and smoke checks rather than a new engine-bound unit test:

1. Build `VEngineWinEditor`.
2. Launch without `--project` and confirm the project-selection view remains.
3. Launch with `--project <DemoProject>` and confirm the project and start scene open without a UI click.
4. Launch with a missing project value and confirm a clear log entry while the Editor remains usable.
5. Run the existing test preset to detect regressions in shared code.
