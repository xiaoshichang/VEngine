#pragma once

#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ve::FileSystem
{
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
    DirectoryEntryType type = DirectoryEntryType::Other;
};

[[nodiscard]] Result<std::string> ReadTextFile(const Path& path);
[[nodiscard]] Result<std::vector<std::byte>> ReadBinaryFile(const Path& path);
[[nodiscard]] Result<void> WriteTextFile(const Path& path, std::string_view text);
[[nodiscard]] Result<void> WriteBinaryFile(const Path& path, const void* data, size_t size);
[[nodiscard]] Result<void> WriteBinaryFile(const Path& path, const std::vector<std::byte>& data);

[[nodiscard]] bool Exists(const Path& path);
[[nodiscard]] bool IsFile(const Path& path);
[[nodiscard]] bool IsDirectory(const Path& path);

[[nodiscard]] Result<void> CreateDirectories(const Path& path);
[[nodiscard]] Result<void> RemoveFile(const Path& path);
[[nodiscard]] Result<std::vector<DirectoryEntry>> ListDirectory(const Path& path);

[[nodiscard]] Path GetCurrentWorkingDirectory();
[[nodiscard]] Path GetExecutableDirectory();
void SetProjectRoot(const Path& path);
[[nodiscard]] const Path& GetProjectRoot();
[[nodiscard]] Path ResolveProjectPath(const Path& relativePath);
}
