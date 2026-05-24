#include "Engine/Runtime/Platform/Windows/Win32MessageLoop.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

namespace ve
{
    Win32MessageLoop::PumpResult Win32MessageLoop::PumpPendingMessages()
    {
        MSG message = {};

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                quitExitCode_ = static_cast<int>(message.wParam);
                return PumpResult::Quit;
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return PumpResult::Continue;
    }

    int Win32MessageLoop::GetQuitExitCode() const noexcept
    {
        return quitExitCode_;
    }
} // namespace ve
