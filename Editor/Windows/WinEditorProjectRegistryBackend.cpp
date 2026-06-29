#include "Editor/Windows/WinEditorProjectRegistryBackend.h"

#include "Editor/Core/EditorProjectRegistry.h"
#include "Engine/Runtime/Logging/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

namespace ve::editor
{
    namespace
    {
        constexpr const wchar_t* RecentProjectsRegistryPath = L"Software\\VEngine\\Editor\\RecentProjects";

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

            std::wstring result(static_cast<size_t>(requiredLength), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), requiredLength);
            return result;
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

            std::string result(static_cast<size_t>(requiredLength), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), requiredLength, nullptr, nullptr);
            return result;
        }

        [[nodiscard]] std::wstring ProjectValueName(std::size_t index)
        {
            return L"Project" + std::to_wstring(index);
        }

        [[nodiscard]] bool ContainsProjectPath(std::span<const std::string> projectPaths, const std::string& path)
        {
            return std::find(projectPaths.begin(), projectPaths.end(), path) != projectPaths.end();
        }
    } // namespace

    std::vector<std::string> WinEditorProjectRegistryBackend::LoadRecentProjects()
    {
        std::vector<std::string> recentProjects;

        HKEY registryKey = nullptr;
        const LSTATUS openStatus = RegOpenKeyExW(HKEY_CURRENT_USER, RecentProjectsRegistryPath, 0, KEY_READ, &registryKey);
        if (openStatus == ERROR_FILE_NOT_FOUND)
        {
            return recentProjects;
        }

        if (openStatus != ERROR_SUCCESS)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to open recent projects registry key: {}", openStatus);
            return recentProjects;
        }

        for (std::size_t index = 0; index < EditorProjectRegistry::MaxRecentProjectCount; ++index)
        {
            const std::wstring valueName = ProjectValueName(index);
            DWORD valueType = 0;
            DWORD byteCount = 0;
            LSTATUS queryStatus = RegQueryValueExW(registryKey, valueName.c_str(), nullptr, &valueType, nullptr, &byteCount);
            if (queryStatus == ERROR_FILE_NOT_FOUND)
            {
                continue;
            }

            if (queryStatus != ERROR_SUCCESS || valueType != REG_SZ || byteCount == 0)
            {
                continue;
            }

            std::wstring widePath(byteCount / sizeof(wchar_t), L'\0');
            queryStatus = RegQueryValueExW(registryKey, valueName.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(widePath.data()), &byteCount);
            if (queryStatus != ERROR_SUCCESS)
            {
                continue;
            }

            while (!widePath.empty() && widePath.back() == L'\0')
            {
                widePath.pop_back();
            }

            std::string path = WideToUtf8(widePath);
            if (!path.empty() && !ContainsProjectPath(recentProjects, path))
            {
                recentProjects.push_back(std::move(path));
            }
        }

        RegCloseKey(registryKey);
        return recentProjects;
    }

    void WinEditorProjectRegistryBackend::SaveRecentProjects(std::span<const std::string> projectPaths)
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, RecentProjectsRegistryPath);

        HKEY registryKey = nullptr;
        const LSTATUS createStatus =
            RegCreateKeyExW(HKEY_CURRENT_USER, RecentProjectsRegistryPath, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &registryKey, nullptr);
        if (createStatus != ERROR_SUCCESS)
        {
            VE_LOG_WARN_CATEGORY("Editor", "Failed to create recent projects registry key: {}", createStatus);
            return;
        }

        std::size_t savedCount = 0;
        for (const std::string& path : projectPaths)
        {
            if (path.empty())
            {
                continue;
            }

            const std::wstring widePath = Utf8ToWide(path);
            if (widePath.empty())
            {
                continue;
            }

            const std::wstring valueName = ProjectValueName(savedCount);
            const DWORD byteCount = static_cast<DWORD>((widePath.size() + 1) * sizeof(wchar_t));
            const LSTATUS setStatus = RegSetValueExW(registryKey, valueName.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(widePath.c_str()), byteCount);
            if (setStatus != ERROR_SUCCESS)
            {
                VE_LOG_WARN_CATEGORY("Editor", "Failed to save recent project value: {}", setStatus);
            }

            ++savedCount;
            if (savedCount >= EditorProjectRegistry::MaxRecentProjectCount)
            {
                break;
            }
        }

        RegCloseKey(registryKey);
    }

    std::unique_ptr<EditorProjectRegistryBackend> CreateWinEditorProjectRegistryBackend()
    {
        return std::make_unique<WinEditorProjectRegistryBackend>();
    }
} // namespace ve::editor
