#include "Engine/Runtime/FileSystem/FileSystem.h"

#include "Engine/Runtime/Core/Platform.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#if VE_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#ifdef GetMessage
#undef GetMessage
#endif
#elif VE_PLATFORM_APPLE
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace ve::FileSystem
{
    namespace
    {
        Path gProjectRoot;

        [[nodiscard]] Error MakeIOError(const char* operation, const Path& path, const std::error_code& error)
        {
            return Error(ErrorCode::IOError, std::string(operation) + " failed for '" + path.GetString() + "': " + error.message());
        }

        [[nodiscard]] Error MakeNotFoundError(const Path& path)
        {
            return Error(ErrorCode::NotFound, "Path does not exist: " + path.GetString());
        }

        [[nodiscard]] Error MakeInvalidArgumentError(const char* message)
        {
            return Error(ErrorCode::InvalidArgument, message);
        }

#if VE_PLATFORM_WINDOWS
        [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);

            if (requiredLength <= 0)
            {
                return {};
            }

            std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wideText.data(), requiredLength);
            return wideText;
        }

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

            std::string utf8Text(static_cast<size_t>(requiredLength), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8Text.data(), requiredLength, nullptr, nullptr);
            return utf8Text;
        }

        [[nodiscard]] std::filesystem::path ToNativePath(const Path& path)
        {
            return std::filesystem::path(Utf8ToWide(path.GetString()));
        }

        [[nodiscard]] Path FromNativePath(const std::filesystem::path& path)
        {
            return Path(WideToUtf8(path.native()));
        }
#else
        [[nodiscard]] std::filesystem::path ToNativePath(const Path& path)
        {
            return std::filesystem::path(path.GetString());
        }

        [[nodiscard]] Path FromNativePath(const std::filesystem::path& path)
        {
            return Path(path.string());
        }
#endif

        [[nodiscard]] DirectoryEntryType GetDirectoryEntryType(const std::filesystem::directory_entry& entry)
        {
            std::error_code error;

            if (entry.is_regular_file(error))
            {
                return DirectoryEntryType::File;
            }

            if (entry.is_directory(error))
            {
                return DirectoryEntryType::Directory;
            }

            return DirectoryEntryType::Other;
        }

        [[nodiscard]] ErrorCode EnsureParentDirectoryExists(const Path& path)
        {
            const Path parentPath = path.GetParentPath();

            if (parentPath.IsEmpty())
            {
                return ErrorCode::None;
            }

            return CreateDirectories(parentPath);
        }

        [[nodiscard]] Result<std::vector<std::byte>> ReadBinaryFileUnchecked(const Path& path)
        {
            std::ifstream file(ToNativePath(path), std::ios::binary);

            if (!file)
            {
                return Result<std::vector<std::byte>>::Failure(MakeIOError("Open", path, std::make_error_code(std::errc::io_error)));
            }

            file.seekg(0, std::ios::end);
            const std::streamoff size = file.tellg();

            if (size < 0)
            {
                return Result<std::vector<std::byte>>::Failure(MakeIOError("Get file size", path, std::make_error_code(std::errc::io_error)));
            }

            file.seekg(0, std::ios::beg);

            std::vector<std::byte> data(static_cast<size_t>(size));

            if (!data.empty())
            {
                file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            }

            if (!file)
            {
                return Result<std::vector<std::byte>>::Failure(MakeIOError("Read", path, std::make_error_code(std::errc::io_error)));
            }

            return Result<std::vector<std::byte>>::Success(std::move(data));
        }

        [[nodiscard]] bool HasUtf8Bom(const std::vector<std::byte>& data)
        {
            return data.size() >= 3 && data[0] == std::byte{0xEF} && data[1] == std::byte{0xBB} && data[2] == std::byte{0xBF};
        }
    } // namespace

    Result<std::string> ReadTextFile(const Path& path)
    {
        Result<std::vector<std::byte>> binaryResult = ReadBinaryFile(path);

        if (!binaryResult)
        {
            return Result<std::string>::Failure(binaryResult.GetError());
        }

        const std::vector<std::byte>& data = binaryResult.GetValue();
        const size_t offset = HasUtf8Bom(data) ? 3 : 0;
        std::string text;

        if (data.size() > offset)
        {
            text.resize(data.size() - offset);
            std::memcpy(text.data(), data.data() + offset, text.size());
        }

        return Result<std::string>::Success(std::move(text));
    }

    Result<std::vector<std::byte>> ReadBinaryFile(const Path& path)
    {
        if (path.IsEmpty())
        {
            return Result<std::vector<std::byte>>::Failure(MakeInvalidArgumentError("ReadBinaryFile requires a non-empty path."));
        }

        if (!Exists(path))
        {
            return Result<std::vector<std::byte>>::Failure(MakeNotFoundError(path));
        }

        if (!IsFile(path))
        {
            return Result<std::vector<std::byte>>::Failure(Error(ErrorCode::InvalidArgument, "Path is not a regular file: " + path.GetString()));
        }

        return ReadBinaryFileUnchecked(path);
    }

    ErrorCode WriteTextFile(const Path& path, std::string_view text)
    {
        return WriteBinaryFile(path, text.data(), text.size());
    }

    ErrorCode WriteBinaryFile(const Path& path, const void* data, size_t size)
    {
        if (path.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        if (data == nullptr && size > 0)
        {
            return ErrorCode::InvalidArgument;
        }

        ErrorCode directoryResult = EnsureParentDirectoryExists(path);

        if (directoryResult != ErrorCode::None)
        {
            return directoryResult;
        }

        std::ofstream file(ToNativePath(path), std::ios::binary | std::ios::trunc);

        if (!file)
        {
            return ErrorCode::IOError;
        }

        if (size > 0)
        {
            file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }

        if (!file)
        {
            return ErrorCode::IOError;
        }

        return ErrorCode::None;
    }

    ErrorCode WriteBinaryFile(const Path& path, const std::vector<std::byte>& data)
    {
        return WriteBinaryFile(path, data.data(), data.size());
    }

    bool Exists(const Path& path)
    {
        std::error_code error;
        return std::filesystem::exists(ToNativePath(path), error);
    }

    bool IsFile(const Path& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(ToNativePath(path), error);
    }

    bool IsDirectory(const Path& path)
    {
        std::error_code error;
        return std::filesystem::is_directory(ToNativePath(path), error);
    }

    ErrorCode CreateDirectories(const Path& path)
    {
        if (path.IsEmpty())
        {
            return ErrorCode::None;
        }

        std::error_code error;
        std::filesystem::create_directories(ToNativePath(path), error);

        if (error)
        {
            return ErrorCode::IOError;
        }

        return ErrorCode::None;
    }

    ErrorCode RemoveFile(const Path& path)
    {
        if (path.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        std::error_code error;
        const bool removed = std::filesystem::remove(ToNativePath(path), error);

        if (error)
        {
            return ErrorCode::IOError;
        }

        if (!removed)
        {
            return ErrorCode::NotFound;
        }

        return ErrorCode::None;
    }

    Result<std::vector<DirectoryEntry>> ListDirectory(const Path& path)
    {
        if (!Exists(path))
        {
            return Result<std::vector<DirectoryEntry>>::Failure(MakeNotFoundError(path));
        }

        if (!IsDirectory(path))
        {
            return Result<std::vector<DirectoryEntry>>::Failure(Error(ErrorCode::InvalidArgument, "Path is not a directory: " + path.GetString()));
        }

        std::vector<DirectoryEntry> entries;
        std::error_code error;

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(ToNativePath(path), error))
        {
            if (error)
            {
                return Result<std::vector<DirectoryEntry>>::Failure(MakeIOError("List directory", path, error));
            }

            const Path entryPath = FromNativePath(entry.path());
            DirectoryEntry directoryEntry;
            directoryEntry.path = entryPath;
            directoryEntry.name = entryPath.GetFilename();
            directoryEntry.type = GetDirectoryEntryType(entry);
            entries.push_back(std::move(directoryEntry));
        }

        if (error)
        {
            return Result<std::vector<DirectoryEntry>>::Failure(MakeIOError("List directory", path, error));
        }

        std::sort(entries.begin(),
                  entries.end(),
                  [](const DirectoryEntry& left, const DirectoryEntry& right) { return left.path.GetString() < right.path.GetString(); });

        return Result<std::vector<DirectoryEntry>>::Success(std::move(entries));
    }

    Path GetCurrentWorkingDirectory()
    {
        std::error_code error;
        const std::filesystem::path currentPath = std::filesystem::current_path(error);

        if (error)
        {
            return Path();
        }

        return FromNativePath(currentPath);
    }

    Path GetExecutableDirectory()
    {
#if VE_PLATFORM_WINDOWS
        std::array<wchar_t, 32768> pathBuffer = {};
        const DWORD length = GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));

        if (length == 0 || length >= pathBuffer.size())
        {
            return Path();
        }

        return FromNativePath(std::filesystem::path(std::wstring(pathBuffer.data(), length)).parent_path());
#elif VE_PLATFORM_APPLE
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string pathBuffer(size, '\0');

        if (_NSGetExecutablePath(pathBuffer.data(), &size) != 0)
        {
            return Path();
        }

        return FromNativePath(std::filesystem::path(pathBuffer.c_str()).parent_path());
#else
        std::array<char, 4096> pathBuffer = {};
        const ssize_t length = readlink("/proc/self/exe", pathBuffer.data(), pathBuffer.size() - 1);

        if (length <= 0)
        {
            return Path();
        }

        pathBuffer[static_cast<size_t>(length)] = '\0';
        return FromNativePath(std::filesystem::path(pathBuffer.data()).parent_path());
#endif
    }

    void SetProjectRoot(const Path& path)
    {
        gProjectRoot = path;
    }

    const Path& GetProjectRoot()
    {
        return gProjectRoot;
    }

    Path ResolveProjectPath(const Path& relativePath)
    {
        if (relativePath.IsAbsolute() || gProjectRoot.IsEmpty())
        {
            return relativePath;
        }

        return JoinPath(gProjectRoot, relativePath);
    }
} // namespace ve::FileSystem
