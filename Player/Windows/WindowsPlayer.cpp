#include "Player/Windows/WindowsPlayer.h"

#include <utility>

namespace ve
{
    WindowsPlayer::WindowsPlayer(ApplicationInitParam initParam)
        : Application(std::move(initParam))
        , viewportClient_("PlayerViewport")
    {
    }

    WindowsPlayer::~WindowsPlayer()
    {
        UnInit();
    }

    const ViewportClient& WindowsPlayer::GetViewportClient() const noexcept
    {
        return viewportClient_;
    }

    ViewportClient& WindowsPlayer::GetViewportClient() noexcept
    {
        return viewportClient_;
    }

    ErrorCode WindowsPlayer::InitializeRendering(Window& mainWindow)
    {
        SyncViewportFromWindow(mainWindow);
        return Application::InitializeRendering(mainWindow);
    }

    void WindowsPlayer::OnMainWindowOSEventInMainThread(const OSEvent& event, Window& mainWindow)
    {
        switch (event.type)
        {
        case OSEventType::WindowResized:
            /// The player viewport tracks the native window's client extent, so resize is the only shell event that
            /// requires rebuilding the viewport binding.
            SyncViewportFromWindow(mainWindow);
            break;
        default:
            break;
        }
    }

    void WindowsPlayer::SyncViewportFromWindow(Window& mainWindow)
    {
        viewportClient_.SyncFromWindow(mainWindow);
    }
} // namespace ve
