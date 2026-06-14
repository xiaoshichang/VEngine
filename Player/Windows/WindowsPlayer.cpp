#include "Player/Windows/WindowsPlayer.h"

#include "Engine/Runtime/Scene/SceneSystem.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

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

        const ErrorCode renderResult = Application::InitializeRendering(mainWindow);
        if (renderResult != ErrorCode::None)
        {
            return renderResult;
        }

        RegisterSceneThreadViewportCallback();
        return ErrorCode::None;
    }

    void WindowsPlayer::RegisterSceneThreadViewportCallback()
    {
        SceneSystem& sceneSystem = GetRuntime().GetSceneSystem();
        sceneSystem.SetRuntimeOSEventCallback([this](const OSEvent& event) { HandleSceneThreadOSEvent(event); });
    }

    void WindowsPlayer::HandleSceneThreadOSEvent(const OSEvent& event)
    {
        VE_ASSERT_SCENE_THREAD();

        switch (event.type)
        {
        case OSEventType::WindowResized:
            if (event.width == 0 || event.height == 0)
            {
                break;
            }

            // Scene Thread owns render-facing viewport state after startup, so resize is applied here instead of on
            // the Main Thread. The binding itself stays window-backed; only the extent changes.
            viewportClient_.ResizeWindowSurface(WindowExtent{event.width, event.height});
            break;
        default:
            break;
        }
    }
} // namespace ve
