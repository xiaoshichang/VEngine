# VEngine Core Module Design

## 1. Purpose

`Core` is the lowest-level shared foundation of VEngine. It provides the common language used by every runtime module:
basic types, build and platform macros, source locations, assertions, recoverable error objects, result values, and small
utility types.

`Core` must stay small and stable. It should make higher-level modules easier to write without becoming an application
framework or a dumping ground for unrelated systems.

## 2. Design Decisions

The first-stage `Core` design follows these decisions:

- `Core` may be depended on by all runtime modules.
- `Core` does not depend on RHI, Platform, Logging, Memory, Threading, Math, Reflection, Resource, Scene, UI, Scripting,
  Editor, Player, or tools.
- `Core` first-stage implementation should use the C++20 standard library directly and avoid Boost unless a later design
  explicitly justifies it.
- Runtime application lifecycle code does not belong in `Core`. The skeleton `Application` class lives in
  `Engine/Runtime/Application`.
- Math primitives, allocators, thread wrappers, jobs, file systems, and timers are separate modules.
- Public macros use the `VE_` prefix.
- Recoverable failures use `Error` and `Result<T>` instead of C++ exceptions.
- Windows tests use CMake/CTest registration and project-owned assertions.

## 3. Responsibilities

`Core` owns:

- Fixed-width type aliases.
- Build configuration detection.
- Compiler detection and compiler helper macros.
- Platform feature macros and debug-break abstraction.
- Source location abstraction based on C++20.
- Assertion reporting and assertion macros.
- Recoverable error representation.
- `Result<T>` for fallible value-returning APIs.
- Small standalone utility types such as `NonCopyable`, `ScopeExit`, and enum flag helpers.

## 4. Non-Responsibilities

`Core` does not own:

- Application lifecycle or main loop ownership.
- Window creation, platform messages, dynamic libraries, or file path conversion.
- Logging backend initialization or log sinks.
- Math types such as vectors, matrices, quaternions, transforms, and bounds.
- Allocators or allocation tracking.
- Thread wrappers, job system, IO thread, or synchronization facade.
- Reflection, serialization, resource loading, scene model, rendering, UI, scripting, editor code, or asset import.

If a utility needs platform APIs, persistent global services, background workers, or ownership of runtime behavior, it
should usually live outside `Core`.

## 5. Dependency Rules

Allowed dependencies for first-stage `Core`:

```text
C++20 standard library
Compiler intrinsics guarded by compiler macros
Platform-neutral preprocessor definitions from CMake
```

Forbidden dependencies for first-stage `Core`:

```text
Boost.Log
RHI
Platform module
Logging module
Memory module
Threading module
Math module
Reflection module
Resource module
Scene module
Editor or Player code
```

`Core` may expose hooks that other modules connect later. For example, assertions may expose an assertion handler, but
they should not directly call the logging facade in the first version.

## 6. Planned File Layout

The first implementation should add files only when they contain useful tracked content. Do not create empty placeholder
directories.

```text
Engine/Runtime/Core/
  BuildConfig.h
  Compiler.h
  Platform.h
  Types.h

  SourceLocation.h

  Assert.h
  Assert.cpp

  Error.h
  Error.cpp
  Result.h

  NonCopyable.h
  ScopeExit.h
  EnumFlags.h
```

The skeleton application wrapper lives outside `Core`:

```text
Engine/Runtime/Application/Application.h
Engine/Runtime/Application/Application.cpp
```

This keeps runtime application lifecycle code separate from the lowest-level shared utilities.

## 7. Build, Compiler, And Platform Helpers

`BuildConfig.h`, `Compiler.h`, and `Platform.h` provide project-wide compile-time facts and small compiler helpers.

Expected public macros:

```cpp
VE_BUILD_DEBUG
VE_BUILD_RELEASE

VE_COMPILER_MSVC
VE_COMPILER_CLANG
VE_COMPILER_GCC

VE_PLATFORM_WINDOWS
VE_PLATFORM_IOS
VE_PLATFORM_APPLE

VE_FORCE_INLINE
VE_NOINLINE
VE_DEBUG_BREAK
```

Guidelines:

- CMake remains responsible for defining target platform feature macros such as `VE_PLATFORM_WINDOWS`.
- Header fallbacks should make undefined platform macros evaluate to `0` when practical.
- `VE_DEBUG_BREAK` should use `__debugbreak()` on MSVC and `__builtin_debugtrap()` on Clang-compatible compilers.
- `VE_FORCE_INLINE` and `VE_NOINLINE` should map to compiler-specific attributes.
- Keep these headers lightweight and include only what is necessary.

## 8. Basic Types

`Types.h` defines engine-wide aliases that match the project's PascalCase style.

Expected aliases:

```cpp
using Int8 = std::int8_t;
using UInt8 = std::uint8_t;
using Int16 = std::int16_t;
using UInt16 = std::uint16_t;
using Int32 = std::int32_t;
using UInt32 = std::uint32_t;
using Int64 = std::int64_t;
using UInt64 = std::uint64_t;
using Float32 = float;
using Float64 = double;
using SizeT = std::size_t;
```

Guidelines:

- Do not introduce custom container aliases in the first `Core` implementation.
- Do not introduce a custom string class in the first `Core` implementation.
- Prefer standard library types unless an engine type adds a clear cross-module benefit.

## 9. Source Location

`SourceLocation.h` wraps C++20 call-site information.

First implementation:

```cpp
#include <source_location>

namespace ve
{
using SourceLocation = std::source_location;
}
```

Usage examples:

```cpp
ve::Error MakeError(
    ve::ErrorCode code,
    std::string message,
    ve::SourceLocation location = ve::SourceLocation::current());
```

Notes:

- `SourceLocation` captures file, function, line, and column information.
- It is useful for errors, logging, profiling, and diagnostics.
- It does not replace assertion macros, because assertion macros are still needed to capture the failed expression text.

## 10. Assertion System

The assertion system should catch developer mistakes while keeping higher-level behavior replaceable.

Expected public types:

```cpp
namespace ve
{
struct AssertionInfo
{
    const char* expression;
    const char* message;
    const char* file;
    const char* function;
    int line;
};

using AssertionHandler = void (*)(const AssertionInfo& info);

void SetAssertionHandler(AssertionHandler handler) noexcept;
AssertionHandler GetAssertionHandler() noexcept;
void ReportAssertionFailure(const AssertionInfo& info) noexcept;
}
```

Expected public macros:

```cpp
VE_ASSERT(expression)
VE_ASSERT_MESSAGE(expression, message)

VE_VERIFY(expression)
VE_VERIFY_MESSAGE(expression, message)

VE_ASSERT_ALWAYS(expression)
VE_ASSERT_ALWAYS_MESSAGE(expression, message)

VE_UNREACHABLE()
```

Behavior:

```text
VE_ASSERT
  Debug: evaluate expression, report failure, trigger debug break.
  Release: compile out and do not evaluate expression.

VE_VERIFY
  Debug: same failure behavior as VE_ASSERT.
  Release: still evaluate expression, but do not report or break.

VE_ASSERT_ALWAYS
  Debug and Release: evaluate expression, report failure on false.

VE_UNREACHABLE
  Marks a code path that should not execute.
```

Default failure policy:

- Debug builds should trigger a breakpoint after reporting.
- If no debugger is attached, the first version may continue execution.
- The first version should not call `std::abort()` by default.
- Later builds may add an assertion policy switch if stricter behavior is needed.

Handler guidelines:

- The default handler should be minimal and must not depend on the Logging module.
- Windows may write to debugger output as a platform-guarded implementation detail if it can be done without depending on
  the Platform module.
- Tests can replace the handler to capture assertion failures.
- Replacing the handler should be safe enough for tests and initialization code. Use standard-library atomics rather than
  Threading-module facilities.

## 11. Error

`Error` represents a recoverable failure. It is intended for normal control flow where failure is expected and should be
handled by the caller.

Expected error codes:

```cpp
namespace ve
{
enum class ErrorCode
{
    None,
    Unknown,
    InvalidArgument,
    InvalidState,
    NotFound,
    AlreadyExists,
    OutOfMemory,
    IOError,
    PlatformError,
    Unsupported,
    Timeout,
    Cancelled,
};
}
```

Expected public API shape:

```cpp
namespace ve
{
class Error
{
public:
    Error();
    explicit Error(ErrorCode code);
    Error(ErrorCode code, std::string message);

    [[nodiscard]] bool IsOk() const noexcept;
    [[nodiscard]] ErrorCode GetCode() const noexcept;
    [[nodiscard]] const std::string& GetMessage() const noexcept;

private:
    ErrorCode code_;
    std::string message_;
};

[[nodiscard]] const char* ToString(ErrorCode code) noexcept;
}
```

Rules:

- `ErrorCode::None` means success.
- `Error` owns its message with `std::string`.
- Empty messages are valid.
- Module-specific error domains are not part of the first implementation.
- Platform error mapping, such as HRESULT or NSError conversion, belongs in later Platform/RHI integration work.

## 12. Result

`Result<T>` represents either a successful value or an `Error`. It is only used when success returns a value.
Fallible APIs that do not return a value return `ErrorCode` directly.

Expected public API shape:

```cpp
namespace ve
{
template <typename T>
class Result
{
public:
    static Result Success(T value);
    static Result Failure(Error error);

    [[nodiscard]] bool IsOk() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

    [[nodiscard]] T& GetValue();
    [[nodiscard]] const T& GetValue() const;
    [[nodiscard]] T&& MoveValue();

    [[nodiscard]] const Error& GetError() const;
};

}
```

Requirements:

- `Result<T>` must support move-only value types such as `std::unique_ptr<T>`.
- `operator bool()` is equivalent to `IsOk()`.
- Calling `GetValue()` on a failure is an API misuse and should be guarded by `VE_ASSERT`.
- Calling `GetError()` on a success is an API misuse and should be guarded by `VE_ASSERT`.
- `Result<void>` is not part of the API. Use `ErrorCode` for no-value success/failure reporting.
- `Result<T>` should not throw exceptions as part of normal use.

Implementation notes:

- `std::variant<T, Error>` is acceptable for the first implementation if it keeps the code simple and supports move-only
  values.
- Avoid hidden heap allocation beyond what `T` or `Error` already performs.
- Keep the API small until real module use cases justify additional helpers.

## 13. Utility Types

### 13.1 NonCopyable

`NonCopyable.h` provides base types for classes that must not be copied.

Expected types:

```cpp
namespace ve
{
class NonCopyable;
class NonMovable;
}
```

Guidelines:

- Constructors and destructors should be protected and defaulted.
- Copy and move operations should be explicitly deleted where appropriate.

### 13.2 ScopeExit

`ScopeExit.h` provides RAII cleanup for local scope rollback.

Expected behavior:

- Runs the stored callable when leaving scope.
- Supports move construction.
- Does not copy.
- Allows dismissal when cleanup is no longer needed.

Example:

```cpp
auto cleanup = ve::MakeScopeExit([&]()
{
    RestorePreviousState();
});
```

### 13.3 EnumFlags

`EnumFlags.h` provides helpers for strongly typed bit flags.

Expected goals:

- Enable `|`, `&`, `^`, `~`, `|=`, `&=`, and `^=` for explicitly opted-in enum classes.
- Keep opt-in explicit to avoid accidentally treating every enum as a bit mask.
- Use the enum underlying type for storage.

Example opt-in shape:

```cpp
template <>
struct EnableEnumFlags<MyFlags> : std::true_type
{
};
```

## 14. First Implementation Scope

The first `Core` implementation should do the following:

1. Add `BuildConfig.h`, `Compiler.h`, `Platform.h`, and `Types.h`.
2. Add `SourceLocation.h`.
3. Add `Assert.h` and `Assert.cpp`.
4. Add `Error.h` and `Error.cpp`.
5. Add `Result.h`.
6. Add `NonCopyable.h`, `ScopeExit.h`, and `EnumFlags.h`.
7. Keep the skeleton `Application` class outside `Engine/Runtime/Core`.
8. Update CMake target sources for `VEngine`.
9. Extend `VEngineTests` with CTest-registered coverage for the new Core APIs.

The first implementation should not add:

- Logging backend integration.
- Custom allocators.
- Math primitives.
- Threading or job system types.
- FileSystem APIs.
- Platform error translation.
- Error domains.
- Exception-based error handling.

## 15. Testing Strategy

Tests remain registered through CMake/CTest.

Recommended first tests:

```text
Core Types
  - Fixed-width aliases have expected sizes.

Core Error
  - Default Error is success.
  - Non-None ErrorCode is failure.
  - Error messages are preserved.
  - ToString(ErrorCode) returns stable non-empty text.

Core Result
  - Result<T> success stores a value.
  - Result<T> failure stores an error.
  - Result<T> supports move-only values.

Core Assert
  - Custom assertion handler receives expression, message, file, function, and line.
  - VE_VERIFY evaluates expressions.
  - Assertion handler can be restored.

Core Utility
  - ScopeExit runs on scope exit and can be dismissed.
  - NonCopyable and NonMovable have the expected type traits.
  - EnumFlags operators work only for opted-in enums.
```

Assertion tests must avoid breaking into the debugger. Prefer testing `ReportAssertionFailure` directly or replacing the
assertion handler and using non-breaking code paths where practical.

## 16. Completion Criteria

`Core` first implementation is complete when:

- The planned files exist and are part of the `VEngine` target.
- `Application` remains outside `Engine/Runtime/Core`.
- No Core file depends on higher-level VEngine modules.
- Windows `windows-msvc-tests` configure, build, and CTest pass.
- Public APIs have concise comments where behavior is not obvious.
- `Docs/CoreModuleDesign.md` still matches the implemented first-stage behavior.
