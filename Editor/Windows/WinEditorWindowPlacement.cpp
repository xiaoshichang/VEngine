#include "Editor/Windows/WinEditorWindowPlacement.h"

#include "Engine/Runtime/Platform/DebugConsole.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

namespace ve::editor
{
    namespace
    {
        void PlaceWindowAtPrimaryWorkAreaTopLeft(HWND window)
        {
            if (window == nullptr || IsWindow(window) == FALSE)
            {
                return;
            }

            RECT workArea = {};
            if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE)
            {
                return;
            }

            (void)SetWindowPos(window, nullptr, workArea.left, workArea.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    } // namespace

    void PlaceWindowsEditorStartupWindows(void* nativeWindowHandle)
    {
        HWND editorWindow = static_cast<HWND>(nativeWindowHandle);
        if (editorWindow == nullptr || IsWindow(editorWindow) == FALSE)
        {
            return;
        }

        PlaceWindowAtPrimaryWorkAreaTopLeft(editorWindow);
        PlaceDebugConsoleNearWindow(nativeWindowHandle);
    }
} // namespace ve::editor
