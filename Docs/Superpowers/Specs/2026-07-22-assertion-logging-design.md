# Assertion Logging Design

## Goal

Ensure every assertion raised after logging initialization is written to the configured VEngine log before the assertion applies its debug-break policy, while preserving enough backend diagnostics to identify graphics-pipeline failures.

## Scope

- Route `VE_ASSERT`, `VE_ASSERT_MESSAGE`, `VE_ASSERT_ALWAYS`, and `VE_ASSERT_ALWAYS_MESSAGE` failures through the logging facade after logging initialization.
- Preserve the existing stderr assertion output as the pre-initialization and emergency fallback.
- Record expression, message, original file and line, and original function in one fatal log record.
- Flush the fatal record before returning to the assertion macro and triggering `VE_DEBUG_BREAK()`.
- Preserve D3D12 InfoQueue validation text for failed root-signature and graphics-pipeline creation calls in `RhiDevice::GetLastErrorMessage()`.
- Report a failed D3D12 InfoQueue callback registration instead of silently losing the callback path.

## Architecture

`Core/Assert` remains independent of `Logging`. Its existing process-wide `AssertionHandler` extension point remains the only connection between the two systems.

`InitializeLogging()` installs a logging-owned assertion handler only after all configured sinks are ready. `ShutdownLogging()` restores the default assertion handler before removing those sinks. Assertions outside that lifetime continue to use the current stderr handler.

The logging-owned handler formats one complete diagnostic and sends it through `LogMessage` with severity `Fatal` and category `Assert`. It then explicitly flushes the logging sinks before returning. The normal assertion macro remains responsible for the debug break; logging does not terminate the process or alter release/debug assertion policy.

## Assertion Failure Flow

1. An assertion macro constructs `AssertionInfo` at the original call site.
2. `ReportAssertionFailure()` invokes the active handler.
3. When logging is initialized, the logging assertion handler emits a fatal record containing all fields from `AssertionInfo`.
4. The logging facade flushes configured sinks.
5. Control returns to the assertion macro.
6. Debug builds execute `VE_DEBUG_BREAK()`; release behavior remains unchanged.

If the handler is entered recursively, or normal log emission throws, it writes the same diagnostic directly to stderr and the platform debugger output. A thread-local recursion guard prevents a logging callback or sink failure from causing an infinite assertion/logging loop. The handler is `noexcept` and catches all exceptions.

## D3D12 Diagnostic Capture

The D3D12 debug callback remains useful for immediate standalone error records, but pipeline failure reporting must not depend on successful callback registration.

Before serializing a root signature or creating a graphics pipeline, the backend records the current InfoQueue message count when an InfoQueue is available. If the operation fails, it reads validation messages added since that count and appends their IDs and descriptions to the operation's HRESULT error. `ShaderManager` already includes `RhiDevice::GetLastErrorMessage()` in its assertion message, so the unified assertion handler will persist both the pipeline identity and the exact D3D12 validation reason.

If `RegisterMessageCallback` fails, D3D12 records a warning with the HRESULT. InfoQueue storage remains available for failure capture even when the callback is unavailable.

## Error Record Shape

An assertion record uses a stable multiline payload:

```text
VEngine assertion failed
Expression: pipeline != nullptr
Message: ShaderManager failed to create graphics pipeline. ... backendError='...'
File: <source path>:<line>
Function: <function name>
```

The normal logger prefix supplies timestamp, fatal severity, `Assert` category, and thread ID.

## Error Handling

- Logging initialization failure leaves the default stderr handler installed.
- Logging shutdown restores the default handler before sink teardown.
- Recursive assertion logging bypasses callbacks and Boost.Log and writes emergency output directly.
- Exceptions from formatting, logging, or flushing are swallowed only after emergency output is attempted.
- D3D12 diagnostic collection is best-effort. Without the debug layer, the backend still reports the operation and HRESULT.

## Verification

- Build the current Windows Editor and Player targets.
- Run the existing CTest suite; do not add an RHI- or renderer-bound unit-test executable.
- Trigger one controlled assertion after `InitializeLogging()` and verify that `VEngine.log` contains expression, custom message, original source location, and function before the debug break.
- Exercise a D3D12 pipeline mismatch in a diagnostic run and verify that the assertion record includes the InfoQueue validation description in `backendError`.
- Verify that assertions before logging initialization still reach stderr without recursion or termination inside the handler.

## Non-Goals

- Changing which assertion macros are compiled in release builds.
- Converting assertions into recoverable errors or exceptions.
- Adding crash dumps, stack traces, or a general crash reporter.
- Making Core depend directly on Logging.
- Adding engine-bound assertion or RHI unit tests.
