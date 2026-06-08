# VEngine Coding Style

## 1. Purpose

This document defines how VEngine code should be written and formatted.

It focuses on coding style: naming, formatting, file organization, includes, comments, ownership expression, API shape, CMake style, C# style, shader style, JSON style, and tests.

Architecture rules belong in `Docs/ArchitectureOverview.md`. Milestones and implementation order belong in `Docs/DevelopmentPlan.md`.

## 2. General Style

VEngine uses a custom mixed style:

```text
Modern C++20
Engine-oriented module clarity
Conservative formatting
Explicit ownership and error handling
```

Use clear code over clever code. Prefer readable, direct implementations unless abstraction removes real duplication or makes ownership and intent clearer.

## 3. Formatting

Formatting is enforced through `.clang-format`.

Rules:

- Use 4 spaces for indentation.
- Do not use tabs.
- Use a 120-column limit.
- Use Allman braces.
- Indent declarations and definitions inside namespaces.
- Use `.h` and `.cpp` for C++.
- Use `.mm` for Objective-C++.
- Use `.metal` for Metal shader files.

Example:

```cpp
void Renderer::Tick(float deltaTime)
{
    if (!initialized_)
    {
        return;
    }

    SubmitFrame(deltaTime);
}
```

## 4. Naming

### 4.1 Namespace

Use `ve` as the root namespace.

```cpp
namespace ve
{
    class Application;

    struct Example
    {
        int value = 0;
    };
}
```

Use nested namespaces for subsystem-specific code:

```cpp
namespace ve::rhi
{
    class Device;
}
```

### 4.2 Types

Use `PascalCase` for classes, structs, enums, and type aliases.

```cpp
class RenderDevice;
struct Vector3;
enum class ResourceState;
using ResourceId = uint64_t;
```

Do not use Unreal-style type prefixes such as `FVector`, `TArray`, or `UObject`.

### 4.3 Functions

Use `PascalCase` for functions and methods.

```cpp
scene.LoadFromFile(path);
renderer.SubmitFrame();
```

### 4.4 Variables

Use `lowerCamelCase` for local variables and parameters.

```cpp
void SetFrameIndex(uint32_t frameIndex)
{
    uint32_t nextFrameIndex = frameIndex + 1;
}
```

Use a trailing underscore for member variables.

```cpp
class Renderer
{
private:
    RenderDevice* renderDevice_ = nullptr;
};
```

### 4.5 Constants

Use `PascalCase` for `constexpr` constants.

```cpp
constexpr uint32_t MaxFramesInFlight = 3;
```

### 4.6 Enum Values

Use `PascalCase` for `enum class` values.

```cpp
enum class TextureFormat
{
    Unknown,
    Rgba8Unorm,
    D24S8,
};
```

### 4.7 Macros

Use `VE_UPPER_SNAKE_CASE` for macros.

```cpp
VE_ASSERT(condition);
VE_LOG_INFO("Renderer initialized");
```

Macros should be limited to narrow infrastructure needs such as assertions, logging, platform detection, export/import annotations, and reflection metadata.

## 5. Files And Directories

Use `PascalCase` for C++ source and header files. The file name should usually match the primary type in the file.

```text
RenderDevice.h
RenderDevice.cpp
GameObject.h
GameObject.cpp
D3D12RenderDevice.h
D3D12RenderDevice.cpp
```

Use direct module folders in the first stage:

```text
Engine/Runtime/Core/Error.h
Engine/Runtime/Core/Error.cpp
Engine/Runtime/Application/Application.h
Engine/Runtime/Application/Application.cpp
Engine/Runtime/Render/Renderer.h
Engine/Runtime/Render/Renderer.cpp
```

Do not introduce module-level `Public/Private` folders unless the project outgrows the direct layout.

## 6. Includes

Use `#pragma once` for header guards.

Include order:

```cpp
#include "CurrentHeader.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Math/Vector3.h"

#include <boost/json.hpp>

#include <array>
#include <memory>
#include <vector>

#if VE_PLATFORM_WINDOWS
#include <Windows.h>
#endif
```

Order:

```text
1. Current .cpp's matching header
2. VEngine internal headers
3. Third-party library headers
4. C++ standard library headers
5. Platform SDK headers
```

Header rules:

- Prefer forward declarations in headers.
- Include complete definitions in `.cpp` files.
- Avoid large convenience headers in public runtime headers.
- Do not expose platform SDK headers from cross-platform public headers.

## 7. Pointers, References, And Const

Place `*` and `&` next to the type.

```cpp
RenderDevice* device;
const TextureDesc& desc;
void Submit(CommandList* commandList);
```

Use west const.

```cpp
const Vector3& position;
const Texture* texture;
```

## 8. Class Layout

Use this order for larger classes:

```cpp
class Renderer
{
public:
    Renderer();
    ~Renderer();

    bool Initialize();
    void Shutdown();

private:
    void CreateResources();

private:
    RenderDevice* device_ = nullptr;
};
```

Preferred order:

```text
public types / constructors / destructor
public methods
protected methods
private methods
private data
```

Access specifiers are not indented.

```cpp
class Foo
{
public:
    void Bar();

private:
    int value_ = 0;
};
```

## 9. Constructors And Initialization

Initialize members at declaration when possible.

```cpp
class Timer
{
private:
    double elapsedSeconds_ = 0.0;
    bool running_ = false;
};
```

Use constructor initializer lists for parameterized or non-trivial initialization.

```cpp
Vector3::Vector3(float x, float y, float z)
    : x_(x)
    , y_(y)
    , z_(z)
{
}
```

Use `explicit` for single-argument constructors by default.

```cpp
explicit Path(std::string value);
```

## 10. Function Style

Parameter rules:

- Pass small trivially copyable types by value.
- Pass large read-only objects as `const T&`.
- Use `T&` for required non-owning mutable references.
- Use `T*` for optional non-owning objects.
- Avoid output parameters when returning a value or small struct is clearer.

Examples:

```cpp
void SetPosition(Vector3 position);
void SetName(std::string_view name);
Result<Texture> CreateTexture(const TextureDesc& desc);
```

Use `[[nodiscard]]` for results that should not be ignored.

```cpp
[[nodiscard]] bool IsValid() const noexcept;
[[nodiscard]] Result<Shader> CompileShader(const ShaderDesc& desc);
```

Use `noexcept` where it communicates a real guarantee:

- Destructors.
- Move constructors and move assignment when valid.
- `swap`.
- Simple getters.

Do not add `noexcept` mechanically to complex functions.

## 11. `auto`

Use `auto` when the type is obvious or too verbose.

```cpp
auto it = resources.find(id);
auto* device = CreateRenderDevice();
```

Prefer explicit types when the type carries important ownership, value, or error semantics.

```cpp
Result<Shader> result = CompileShader(desc);
std::unique_ptr<Renderer> renderer = CreateRenderer();
```

Do not use `auto` to hide narrowing conversions or unclear factory return types.

## 12. Ownership

Prefer value types by default.

Use `std::unique_ptr` for exclusive ownership.

Use `std::shared_ptr` only when shared ownership is real and intentional.

Use raw pointers or references for non-owning access:

- `T&` means the object must exist.
- `T*` means the object may be absent.

Resource-facing APIs should prefer stable handles or explicit owner types over casual shared ownership.

## 13. Containers And Strings

Use standard containers by default:

```text
std::vector
std::array
std::unordered_map
```

Use Boost.Container or custom containers only when there is a specific reason.

String rules:

- Use `std::string` for owned UTF-8 text.
- Use `std::string_view` for non-owning string parameters.
- Use a `Path` wrapper for file paths.
- Convert UTF-8 and UTF-16 at Windows API boundaries.
- Use forward slashes in project-relative paths.

## 14. Enums And Bitmasks

Use `enum class` for scoped enums.

For bitmask enums, explicitly enable bitmask operators.

```cpp
enum class BufferUsage : uint32_t
{
    Vertex = 1 << 0,
    Index = 1 << 1,
    Uniform = 1 << 2,
};

VE_ENABLE_BITMASK_OPERATORS(BufferUsage);
```

Do not rely on implicit integer conversions from enums.

## 15. Error Handling And Assertions

Runtime code should not use C++ exceptions as normal control flow.

Use `Result<T>`, `ErrorCode`, or clear boolean returns depending on API shape:

- Use `Result<T>` for fallible APIs that return a useful value on success.
- Use `ErrorCode` for fallible APIs that do not return a value. `ErrorCode::None` means success; any non-`None` value is
  a concrete failure.
- Use `bool` for small local checks where the caller can recover simply.
- Use logs to provide context on failure paths.
- Do not use `Result<void>`. If callers need a string form for a no-value failure, define stable text alongside the
  `ErrorCode` value and expose it through `ToString(ErrorCode)` or a module-specific error string helper.

Preferred assertion macros:

```cpp
VE_ASSERT(condition);
VE_ASSERT_MSG(condition, "message");
VE_VERIFY(condition);
VE_UNREACHABLE();
```

Use assertions for programmer errors and invariants. Use error returns for expected runtime failures.

## 16. Logging

All logs should go through the VEngine logging facade.

```cpp
VE_LOG_INFO("Loaded scene {}", sceneName);
VE_LOG_ERROR("Failed to load asset {}", assetPath);
```

Log macros use C++20 `std::format` placeholders.

Rules:

- Do not call `Boost.Log` directly in ordinary engine code.
- Write log messages in English.
- Keep messages short and specific.
- Avoid noisy logs in per-frame hot paths.
- Include enough context to diagnose failure paths.

## 17. Comments

Use comments to explain intent, invariants, lifetime, threading assumptions, or non-obvious behavior.

Do not comment obvious statements.

Use concise Doxygen-style comments for public APIs when helpful.

```cpp
/// Represents a GPU buffer owned by an RHI backend.
class Buffer
{
};
```

Avoid large file header banners unless a project license header is later required.

## 18. Reflection Macros

Reflection macros should only register metadata.

```cpp
VE_REFLECT_TYPE(TransformComponent)
VE_REFLECT_PROPERTY(position)
VE_REFLECT_PROPERTY(rotation)
VE_REFLECT_PROPERTY(scale)
```

Do not use macros to hide complex control flow or ordinary business logic.

Prefer placing reflection registration in `.cpp` files or clearly named registration files.

## 19. JSON Style

Use 4 spaces for JSON indentation.

Use `lowerCamelCase` keys.

Store stable enum names as strings rather than fragile integer values.

Use GUIDs as strings.

Use forward slashes in paths.

Include a `version` field where the file may evolve.

Example:

```json
{
    "version": 1,
    "guid": "00000000-0000-0000-0000-000000000000",
    "assetType": "Material",
    "sourcePath": "Assets/Samples/Cube.fbx"
}
```

## 20. Tests

Use CMake/CTest to register C++ test executables.

Test file names use `PascalCase` with a `Tests` suffix.

```text
Vector3Tests.cpp
ResourceGuidTests.cpp
SceneSerializationTests.cpp
```

Test case names use `PascalCase`.

```cpp
NormalizeReturnsUnitVector()
ParsesValidGuidString()
```

Test names should describe observable behavior.

## 21. CMake Style

CMake commands use lowercase.

Target names use `PascalCase`.

Options use `VE_UPPER_SNAKE_CASE`.

Use 4 spaces for indentation.

Prefer target-scoped CMake commands.

```cmake
add_library(VEngine STATIC)

target_sources(VEngine
    PRIVATE
        Engine/Runtime/Core/Error.cpp
    PUBLIC
        Engine/Runtime/Core/Error.h
)

target_compile_features(VEngine
    PUBLIC
        cxx_std_20
)
```

Do not use global `include_directories` or global `link_libraries` for project code.

## 22. C# Style

C# code should follow .NET-style naming with VEngine's trailing-underscore field convention.

Rules:

- Types, methods, and properties use `PascalCase`.
- Parameters and local variables use `camelCase`.
- Private fields use a trailing underscore.
- Constants use `PascalCase`.
- Namespaces use `VEngine` or `VEngine.Scripting`.

Example:

```csharp
namespace VEngine;

public sealed class Transform : Component
{
    private IntPtr nativeHandle_;

    public Vector3 Position
    {
        get;
        set;
    }

    public override void OnUpdate(float deltaTime)
    {
    }
}
```

## 23. Shader Style

Shader source files use `PascalCase`.

```text
BasicMesh.hlsl
```

HLSL rules:

- Struct names use `PascalCase`.
- Constant buffer names use `PascalCase`.
- Variables use `lowerCamelCase`.
- Entry points use `VSMain`, `PSMain`, or `CSMain`.
- Semantics use standard uppercase names.

Example:

```hlsl
struct VertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

cbuffer CameraConstants : register(b0)
{
    float4x4 viewProjection;
};
```

## 24. Pre-Commit Checks

Before committing code changes:

- Run clang-format on changed C++ and Objective-C++ files.
- Run `VEngineTests` when Windows tests are available.
- Reconfigure CMake after changing CMake files.
- Run relevant smoke tests after changing shader, asset import, scripting, or RHI code.

Keep this checklist practical. The project can add automation as the repository grows.
