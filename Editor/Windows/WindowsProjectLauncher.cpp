#include "Editor/Windows/WindowsProjectLauncher.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <ShlObj.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <string_view>
#include <utility>

namespace ve
{
    namespace
    {
        using boost::json::array;
        using boost::json::object;
        using boost::json::value;

        constexpr wchar_t EditorRegistryKey[] = L"Software\\VEngine\\Editor";
        constexpr wchar_t RecentProjectsValueName[] = L"RecentProjects";
        constexpr int MaxRecentProjects = 10;

        [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredLength = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (requiredLength <= 0)
            {
                return {};
            }

            std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
            MultiByteToWideChar(CP_UTF8,
                                MB_ERR_INVALID_CHARS,
                                text.data(),
                                static_cast<int>(text.size()),
                                wideText.data(),
                                requiredLength);
            return wideText;
        }

        [[nodiscard]] std::string WideToUtf8(std::wstring_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredLength = WideCharToMultiByte(
                CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
            if (requiredLength <= 0)
            {
                return {};
            }

            std::string utf8Text(static_cast<size_t>(requiredLength), '\0');
            WideCharToMultiByte(CP_UTF8,
                                0,
                                text.data(),
                                static_cast<int>(text.size()),
                                utf8Text.data(),
                                requiredLength,
                                nullptr,
                                nullptr);
            return utf8Text;
        }

        [[nodiscard]] std::string GetUtcTimestamp()
        {
            std::time_t now = std::time(nullptr);
            std::tm utc = {};
            gmtime_s(&utc, &now);

            std::array<char, 32> buffer = {};
            std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
            return buffer.data();
        }

        [[nodiscard]] std::string ReadRegistryString()
        {
            DWORD type = REG_SZ;
            DWORD byteCount = 0;
            const LSTATUS sizeResult = RegGetValueW(HKEY_CURRENT_USER,
                                                    EditorRegistryKey,
                                                    RecentProjectsValueName,
                                                    RRF_RT_REG_SZ,
                                                    &type,
                                                    nullptr,
                                                    &byteCount);
            if (sizeResult != ERROR_SUCCESS || byteCount == 0)
            {
                return {};
            }

            std::wstring value(byteCount / sizeof(wchar_t), L'\0');
            const LSTATUS readResult = RegGetValueW(HKEY_CURRENT_USER,
                                                    EditorRegistryKey,
                                                    RecentProjectsValueName,
                                                    RRF_RT_REG_SZ,
                                                    &type,
                                                    value.data(),
                                                    &byteCount);
            if (readResult != ERROR_SUCCESS)
            {
                return {};
            }

            while (!value.empty() && value.back() == L'\0')
            {
                value.pop_back();
            }

            return WideToUtf8(value);
        }

        [[nodiscard]] ErrorCode WriteRegistryString(std::string_view text)
        {
            const std::wstring wideText = Utf8ToWide(text);
            HKEY key = nullptr;
            const LSTATUS keyResult = RegCreateKeyExW(HKEY_CURRENT_USER,
                                                      EditorRegistryKey,
                                                      0,
                                                      nullptr,
                                                      REG_OPTION_NON_VOLATILE,
                                                      KEY_WRITE,
                                                      nullptr,
                                                      &key,
                                                      nullptr);
            if (keyResult != ERROR_SUCCESS)
            {
                return ErrorCode::PlatformError;
            }

            RegCloseKey(key);

            const DWORD byteCount = static_cast<DWORD>((wideText.size() + 1) * sizeof(wchar_t));
            const LSTATUS valueResult = RegSetKeyValueW(HKEY_CURRENT_USER,
                                                        EditorRegistryKey,
                                                        RecentProjectsValueName,
                                                        REG_SZ,
                                                        wideText.c_str(),
                                                        byteCount);
            return valueResult == ERROR_SUCCESS ? ErrorCode::None : ErrorCode::PlatformError;
        }

        [[nodiscard]] std::vector<WindowsRecentProject> ParseRecentProjects(std::string_view text)
        {
            std::vector<WindowsRecentProject> recentProjects;
            if (text.empty())
            {
                return recentProjects;
            }

            boost::system::error_code parseError;
            value root = boost::json::parse(text, parseError);
            if (parseError || !root.is_array())
            {
                return recentProjects;
            }

            for (const value& item : root.as_array())
            {
                if (!item.is_object())
                {
                    continue;
                }

                const object& itemObject = item.as_object();
                WindowsRecentProject recent;

                if (const value* pathValue = itemObject.if_contains("path");
                    pathValue != nullptr && pathValue->is_string())
                {
                    recent.path = Path(std::string(pathValue->as_string()));
                }

                if (const value* nameValue = itemObject.if_contains("name");
                    nameValue != nullptr && nameValue->is_string())
                {
                    recent.displayName = nameValue->as_string().c_str();
                }

                if (const value* guidValue = itemObject.if_contains("guid");
                    guidValue != nullptr && guidValue->is_string())
                {
                    if (Result<AssetGuid> guid = AssetGuid::Parse(std::string(guidValue->as_string())))
                    {
                        recent.guid = guid.GetValue();
                    }
                }

                if (const value* lastOpenedValue = itemObject.if_contains("lastOpenedUtc");
                    lastOpenedValue != nullptr && lastOpenedValue->is_string())
                {
                    recent.lastOpenedUtc = lastOpenedValue->as_string().c_str();
                }

                recent.available = FileSystem::IsFile(recent.path / ".veproject");
                if (!recent.path.IsEmpty())
                {
                    recentProjects.push_back(std::move(recent));
                }
            }

            return recentProjects;
        }

        [[nodiscard]] std::string SerializeRecentProjects(const std::vector<WindowsRecentProject>& recentProjects)
        {
            array root;
            for (const WindowsRecentProject& recentProject : recentProjects)
            {
                object item;
                item["path"] = recentProject.path.GetString();
                item["name"] = recentProject.displayName;
                if (recentProject.guid.IsValid())
                {
                    item["guid"] = recentProject.guid.ToString();
                }
                item["lastOpenedUtc"] = recentProject.lastOpenedUtc;
                root.push_back(std::move(item));
            }

            return boost::json::serialize(root);
        }
    } // namespace

    std::vector<WindowsRecentProject> LoadWindowsRecentProjects()
    {
        std::vector<WindowsRecentProject> recentProjects = ParseRecentProjects(ReadRegistryString());
        if (recentProjects.size() > MaxRecentProjects)
        {
            recentProjects.resize(MaxRecentProjects);
        }

        return recentProjects;
    }

    ErrorCode SaveWindowsRecentProject(const Path& projectRoot, const EditorProjectDescriptor& descriptor)
    {
        std::vector<WindowsRecentProject> recentProjects = LoadWindowsRecentProjects();
        recentProjects.erase(std::remove_if(recentProjects.begin(),
                                            recentProjects.end(),
                                            [&projectRoot](const WindowsRecentProject& recentProject)
                                            { return recentProject.path == projectRoot; }),
                             recentProjects.end());

        WindowsRecentProject recentProject;
        recentProject.path = projectRoot;
        recentProject.displayName = descriptor.displayName;
        recentProject.guid = descriptor.guid;
        recentProject.lastOpenedUtc = GetUtcTimestamp();
        recentProject.available = true;

        recentProjects.insert(recentProjects.begin(), std::move(recentProject));
        if (recentProjects.size() > MaxRecentProjects)
        {
            recentProjects.resize(MaxRecentProjects);
        }

        return WriteRegistryString(SerializeRecentProjects(recentProjects));
    }

    Result<Path> BrowseForWindowsProjectFolder(HWND owner, const wchar_t* title)
    {
        BROWSEINFOW browseInfo = {};
        browseInfo.hwndOwner = owner;
        browseInfo.lpszTitle = title;
        browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

        PIDLIST_ABSOLUTE itemList = SHBrowseForFolderW(&browseInfo);
        if (itemList == nullptr)
        {
            return Result<Path>::Failure(Error(ErrorCode::Cancelled, "Folder browse was cancelled."));
        }

        std::array<wchar_t, MAX_PATH> pathBuffer = {};
        const BOOL pathResult = SHGetPathFromIDListW(itemList, pathBuffer.data());
        CoTaskMemFree(itemList);

        if (pathResult == FALSE)
        {
            return Result<Path>::Failure(Error(ErrorCode::PlatformError, "Selected folder path could not be read."));
        }

        return Result<Path>::Success(Path(WideToUtf8(pathBuffer.data())));
    }
} // namespace ve
