# VEngine FileSystem Design

## 1. Purpose

This document records the first-stage FileSystem design for VEngine.

The goal is to provide a small, explicit, cross-platform file access layer for engine runtime, tools, tests, and future
editor systems. The first implementation focuses on path handling, synchronous file IO, basic directory operations, and
explicit project-root resolution.

This is intentionally not a complete asset database, virtual file system, package system, async IO layer, or file watcher.
Those systems can be layered on top after the early runtime and editor skeleton are stable.

## 2. Current Scope

The module lives under:

```text
Engine/Runtime/FileSystem/Path.h
Engine/Runtime/FileSystem/Path.cpp
Engine/Runtime/FileSystem/FileSystem.h
Engine/Runtime/FileSystem/FileSystem.cpp
```

Current responsibilities:

- Store engine-facing paths as UTF-8 strings.
- Normalize path separators to `/`.
- Normalize `.` and ordinary `..` path segments.
- Preserve Windows drive roots and UNC roots.
- Provide basic path queries such as parent path, filename, extension, and absolute-path detection.
- Read and write text files.
- Read and write binary files.
- Create directories.
- Remove files.
- Query file and directory existence.
- Enumerate a directory non-recursively.
- Track an explicitly configured project root.
- Resolve project-relative paths against the configured project root.

Current non-responsibilities:

- Async IO.
- IO thread scheduling.
- Virtual mount points.
- Package or archive loading.
- Asset GUIDs or asset metadata.
- File watching.
- Recursive asset scanning policies.
- Runtime cooked asset format ownership.
- Permission models or sandboxing.

## 3. API Shape

The first-stage API uses a global facade:

```cpp
namespace ve::FileSystem
{
    Result<std::string> ReadTextFile(const Path& path);
    Result<std::vector<std::byte>> ReadBinaryFile(const Path& path);
    ErrorCode WriteTextFile(const Path& path, std::string_view text);
    ErrorCode WriteBinaryFile(const Path& path, const void* data, size_t size);
    ErrorCode WriteBinaryFile(const Path& path, const std::vector<std::byte>& data);

    bool Exists(const Path& path);
    bool IsFile(const Path& path);
    bool IsDirectory(const Path& path);

    ErrorCode CreateDirectories(const Path& path);
    ErrorCode RemoveFile(const Path& path);
    Result<std::vector<DirectoryEntry>> ListDirectory(const Path& path);

    Path GetCurrentWorkingDirectory();
    Path GetExecutableDirectory();
    void SetProjectRoot(const Path& path);
    const Path& GetProjectRoot();
    Path ResolveProjectPath(const Path& relativePath);
}
```

This is intentionally simple and similar in accessibility to `ve::Time`. FileSystem is basic infrastructure and is
expected to be used by many engine systems.

## 4. Path Representation

The engine-facing path type is `ve::Path`.

`Path` is a thin value type around a UTF-8 string. It normalizes paths at construction time and stores separators as `/`
on every platform.

Rules:

- Internal path text is UTF-8.
- Windows API boundaries convert UTF-8 to UTF-16.
- `\` is converted to `/`.
- Repeated separators are collapsed for regular paths.
- `.` segments are removed.
- Ordinary `..` segments are resolved when there is a previous non-`..` segment.
- Relative paths may retain leading `..` segments when they cannot be resolved.
- Windows drive roots such as `C:/` are preserved.
- UNC roots such as `//server/share` are preserved.
- Case is not normalized.
- A trailing `/` is removed unless the path is a root.

Examples:

```text
Assets\\Textures\\./Brick.png -> Assets/Textures/Brick.png
C:\\VEngine\\..\\Game\\file.txt -> C:/Game/file.txt
../Assets/./Mesh.fbx -> ../Assets/Mesh.fbx
```

`Path` does not represent virtual mount paths in the first implementation. Future mount points can reuse the same UTF-8
and `/` conventions.

## 5. Error Handling

File operations that return data use `Result<T>`. File operations that only report success or failure return
`ErrorCode`.

Examples:

```cpp
Result<std::string> text = FileSystem::ReadTextFile(path);
ErrorCode writeResult = FileSystem::WriteBinaryFile(path, data, size);
```

Simple queries return `bool`:

```cpp
bool exists = FileSystem::Exists(path);
bool directory = FileSystem::IsDirectory(path);
```

Error code guidelines:

- Missing input paths use `ErrorCode::NotFound`.
- Invalid arguments use `ErrorCode::InvalidArgument`.
- Filesystem or OS failures use `ErrorCode::IOError`.
- Unsupported operations use `ErrorCode::Unsupported`.

## 6. Text Files

Text APIs assume UTF-8.

Rules:

- `ReadTextFile()` returns bytes interpreted as UTF-8 text.
- A UTF-8 BOM is stripped when present.
- No broader encoding detection is attempted.
- `WriteTextFile()` writes exactly the provided text.
- `WriteTextFile()` does not add a BOM.
- Newlines are not converted.

## 7. Binary Files

Binary reads return:

```cpp
std::vector<std::byte>
```

The byte vector is intentionally generic and does not imply asset, image, or mesh ownership. Higher-level resource and
asset systems can wrap binary payloads later if needed.

Binary write APIs accept a raw data pointer and byte size. A thin `std::vector<std::byte>` convenience overload is also
available because binary reads use the same container type.

## 8. Directory Operations

The first implementation supports non-recursive directory listing:

```cpp
enum class DirectoryEntryType
{
    File,
    Directory,
    Other,
};

struct DirectoryEntry
{
    Path path;
    std::string name;
    DirectoryEntryType type;
};
```

`ListDirectory()` returns immediate children only. Recursive traversal, filters, stable sorting policies, and asset scan
rules should be introduced by AssetDatabase or tooling milestones.

The implementation may sort directory entries by normalized path for deterministic tests and stable tool output.

## 9. Root Paths

The first implementation exposes:

```cpp
Path GetCurrentWorkingDirectory();
Path GetExecutableDirectory();
void SetProjectRoot(const Path& path);
const Path& GetProjectRoot();
Path ResolveProjectPath(const Path& relativePath);
```

The project root is explicit. The FileSystem does not scan upward for `.git`, `CMakeLists.txt`, `AGENTS.md`, or other
repo markers. Editor and tool code should set the project root when they know it.

`ResolveProjectPath()` behavior:

- Absolute paths are returned normalized.
- Relative paths are joined to the configured project root.
- If the project root is empty, the relative path is returned normalized.

## 10. Project Layout

The standard project root is intentionally minimal:

```text
ProjectRoot/
  VEProject.json
  Assets/
  Library/
```

Editor code can use `ve::editor::EditorProject::EnsureLayout()` to create this shape and
`ve::editor::EditorProject::LoadDescriptor()` / `ve::editor::EditorProject::SaveDescriptor()` to read and write the
descriptor. This logic intentionally lives under `Editor/` instead of `Engine/Runtime/`; runtime code should not own
project creation or editor workspace policy. `Assets/` and `Library/` are code-level conventions, so the descriptor does
not repeat those paths. All JSON emitted by these helpers is pretty-printed with line breaks and four-space indentation.

`VEProject.json` fields in the first schema:

```json
{
    "schemaVersion": 1,
    "name": "DemoProject",
    "engineVersion": "0.1.0",
    "startScene": "Assets/Scenes/SampleScene.vescene"
}
```

`Assets/` stores project-visible content. `Library/` stores generated local content and import/cache products. `Library/`
is part of the directory convention, but future production projects should usually ignore its generated contents.

Repository-level engine assets are grouped under the repository `Assets/` root and remain separate from the project
root:

- `Assets/EditorOnlyAsset/` stores engine-owned Editor-only resources. The Editor build copies this directory under the
  Editor's `Assets/` directory; Player packages do not include it.
- `Assets/BuiltinAsset/` stores engine-owned runtime builtin resources. The Editor build copies this directory under the
  Editor's `Assets/` directory; Player packages include referenced runtime builtin files.
- `ProjectRoot/Assets/` stores project-authored resources. Player packages include referenced project runtime files.

The Editor AssetDatabase currently scans `ProjectRoot/Assets/` and repository `Assets/BuiltinAsset/Engine/` for `.obj`,
`.vematerial`, `.veshader`, and `.vescene` files. `.obj` files are native source assets; missing `.vemesh` descriptors
are generated into `Library/` and reference the source OBJ rather than copying actual mesh data.
`Assets/EditorOnlyAsset/` is not scanned into runtime asset metadata.

## 11. Platform Notes

### Windows

The engine stores paths as UTF-8 but converts to UTF-16 at Windows filesystem boundaries. This keeps Chinese and other
non-ASCII paths viable in early tests and future tools.

The implementation uses `std::filesystem` for file and directory operations, while constructing native paths carefully
from UTF-8.

### macOS

The first implementation does not add AppKit or bundle-resource APIs. The UTF-8 path and synchronous IO layer should still
compile with the macOS toolchain, but macOS app bundle, documents directory, and packaged-resource handling are future work.

## 12. Future Mount Points

Virtual mounts are intentionally deferred.

Expected future mount names may include:

```text
/Engine
/Project
/User
```

The future mount layer should map virtual paths to platform-specific physical roots and preserve the same normalized path
rules used by `ve::Path`.

## 13. Future Async IO

FileSystem remains the synchronous file access foundation.

Async reads should route through IOSystem, which returns `IORequestHandle` values and exposes completed results through
its completion queue. Future ResourceSystem code should decide where those results are consumed and how they are handed
back to Game Thread or asset-loading state. The synchronous APIs in this document remain useful for tests, tools,
startup paths, and small metadata files.

## 14. Testing

The test target is:

```text
VEngineFileSystemTests
```

Planned coverage:

- Path normalization.
- Path joining.
- Parent path, filename, and extension queries.
- Text write/read.
- UTF-8 BOM stripping.
- Binary write/read.
- CreateDirectories.
- Exists, IsFile, and IsDirectory.
- Non-recursive ListDirectory.
- Chinese path smoke test on Windows.
- Missing file returns `ErrorCode::NotFound`.
