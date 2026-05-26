#pragma once

#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

namespace ve
{
    struct WindowsRecentProject
    {
        Path path;
        std::string displayName;
        AssetGuid guid;
        std::string lastOpenedUtc;
        bool available = false;
    };

    struct WindowsProjectLauncherResult
    {
        bool accepted = false;
        bool createProject = false;
        Path projectRoot;
    };

    [[nodiscard]] std::vector<WindowsRecentProject> LoadWindowsRecentProjects();
    [[nodiscard]] ErrorCode SaveWindowsRecentProject(const Path& projectRoot,
                                                     const EditorProjectDescriptor& descriptor);
    [[nodiscard]] Result<Path> BrowseForWindowsProjectFolder(HWND owner, const wchar_t* title);
} // namespace ve
