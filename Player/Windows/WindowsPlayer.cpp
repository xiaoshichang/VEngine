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
        viewportClient_.SyncFromWindow(mainWindow);
        return Application::InitializeRendering(mainWindow);
    }

    void WindowsPlayer::OnMainLoopIteration(Window& mainWindow)
    {
        viewportClient_.SyncFromWindow(mainWindow);
    }
} // namespace ve
