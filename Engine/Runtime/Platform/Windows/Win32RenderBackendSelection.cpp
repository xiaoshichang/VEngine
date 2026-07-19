#include "Engine/Runtime/Platform/Windows/Win32RenderBackendSelection.h"

#include "Engine/Runtime/Render/RenderSystem.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#include <string_view>

namespace ve
{
    RenderBackend SelectWin32RenderBackendFromCommandLine()
    {
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            return RenderBackend::D3D12;
        }

        RenderBackend backend = RenderBackend::D3D12;
        for (int argumentIndex = 1; argumentIndex < argumentCount; ++argumentIndex)
        {
            if (std::wstring_view(arguments[argumentIndex]) == L"--dx12")
            {
                backend = RenderBackend::D3D12;
                break;
            }
        }

        LocalFree(arguments);
        return backend;
    }
} // namespace ve
