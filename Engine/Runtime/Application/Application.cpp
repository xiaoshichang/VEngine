#include "Engine/Runtime/Application/Application.h"

#include "Engine/Runtime/Logging/Log.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

namespace ve
{
    namespace
    {
        struct WindowStateSnapshot
        {
            bool visible = false;
            bool focused = false;
            bool minimized = false;
            WindowExtent extent = {};
        };

        [[nodiscard]] WindowStateSnapshot CaptureWindowState(const Window& window)
        {
            WindowStateSnapshot snapshot;
            snapshot.visible = window.IsVisible();
            snapshot.focused = window.IsFocused();
            snapshot.minimized = window.IsMinimized();
            snapshot.extent = window.GetClientExtent();
            return snapshot;
        }

        void EnqueueOSEvent(SceneSystem& sceneSystem, OSEvent event)
        {
            ErrorCode queueResult = sceneSystem.EnqueueOSEvent(event);
            if (queueResult != ErrorCode::None)
            {
                VE_LOG_WARN("Failed to enqueue OS event for Scene Thread: {}", ToString(queueResult));
            }
        }

        void EnqueueWindowStateDeltaEvents(SceneSystem& sceneSystem,
                                           const WindowStateSnapshot& previousState,
                                           const WindowStateSnapshot& currentState)
        {
            if (previousState.focused != currentState.focused)
            {
                EnqueueOSEvent(sceneSystem,
                               OSEvent{
                                   currentState.focused ? OSEventType::WindowFocusGained : OSEventType::WindowFocusLost,
                               });
            }

            if (previousState.minimized != currentState.minimized)
            {
                const OSEventType minimizedEventType =
                    currentState.minimized ? OSEventType::WindowMinimized : OSEventType::WindowRestored;
                EnqueueOSEvent(sceneSystem,
                               OSEvent{
                                   minimizedEventType,
                               });
            }

            if (previousState.visible != currentState.visible)
            {
                EnqueueOSEvent(sceneSystem,
                               OSEvent{currentState.visible ? OSEventType::WindowShown : OSEventType::WindowHidden});
            }

            if (previousState.extent.width != currentState.extent.width ||
                previousState.extent.height != currentState.extent.height)
            {
                EnqueueOSEvent(sceneSystem,
                               OSEvent{
                                   OSEventType::WindowResized,
                                   currentState.extent.width,
                                   currentState.extent.height,
                               });
            }
        }

        void ShutdownEngineRuntime(EngineRuntime& runtime)
        {
            runtime.Shutdown();
        }
    } // namespace

    Application::Application(std::string name)
        : initParam_()
    {
        initParam_.name = std::move(name);

        if (initParam_.name.empty())
        {
            initParam_.name = "VEngine";
        }

        initParam_.mainWindow.title = initParam_.name;
    }

    Application::Application(ApplicationInitParam desc)
        : initParam_(std::move(desc))
    {
        if (initParam_.name.empty())
        {
            initParam_.name = "VEngine";
        }

        if (initParam_.mainWindow.title.empty())
        {
            initParam_.mainWindow.title = initParam_.name;
        }
    }

    Application::~Application() = default;

    int Application::Run()
    {
        exitCode_ = RunApplication();
        return exitCode_;
    }

    ErrorCode Application::InitializeEngineRuntime()
    {
        ErrorCode runtimeResult = engineRuntime_.Initialize(initParam_.runtime);

        if (runtimeResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Failed to initialize engine runtime: {}", ToString(runtimeResult));
        }

        return runtimeResult;
    }

    Result<std::unique_ptr<Window>> Application::CreateMainWindow()
    {
        Result<std::unique_ptr<Window>> windowResult = Window::Create(initParam_.mainWindow);

        if (!windowResult)
        {
            VE_LOG_ERROR("Failed to create main window: {}", windowResult.GetError().GetMessage());
        }

        return windowResult;
    }

    ErrorCode Application::InitializeRendering(Window& mainWindow)
    {
        RenderSystem& renderSystem = engineRuntime_.GetRenderSystem();

        ErrorCode deviceResult = renderSystem.InitializeDevice(initParam_.runtime.renderSystem.device);
        if (deviceResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Failed to initialize render device: {}", ToString(deviceResult));
            return deviceResult;
        }

        const WindowExtent extent = mainWindow.GetClientExtent();
        RenderSurfaceDesc surfaceDesc;
        surfaceDesc.nativeWindow = mainWindow.GetNativeHandle();
        surfaceDesc.nativeLayer = mainWindow.GetNativeLayer();
        surfaceDesc.width = extent.width;
        surfaceDesc.height = extent.height;

        ErrorCode swapchainResult = renderSystem.CreateMainSwapchain(surfaceDesc);
        if (swapchainResult != ErrorCode::None)
        {
            VE_LOG_ERROR("Failed to create main render swapchain: {}", ToString(swapchainResult));
            renderSystem.ShutdownDevice();
            return swapchainResult;
        }

        return ErrorCode::None;
    }

    void Application::ShutdownRendering() noexcept
    {
        RenderSystem& renderSystem = engineRuntime_.GetRenderSystem();
        renderSystem.DestroyMainSwapchain();
        renderSystem.ShutdownDevice();
    }

    int Application::RunMainLoop(Window& mainWindow)
    {
        RenderSystem& renderSystem = engineRuntime_.GetRenderSystem();
        SceneSystem& sceneSystem = engineRuntime_.GetSceneSystem();
        int exitCode = 0;
        WindowStateSnapshot previousState = CaptureWindowState(mainWindow);

        while (!mainWindow.ShouldClose())
        {
            mainWindow.PumpCommands();

            // Main Thread handles loop control events (for example WM_QUIT) directly.
            const WindowPumpStatus pumpStatus = mainWindow.PumpEvents();
            const WindowStateSnapshot currentState = CaptureWindowState(mainWindow);
            EnqueueWindowStateDeltaEvents(sceneSystem, previousState, currentState);
            previousState = currentState;

            // Remaining window state changes are forwarded to Scene Thread through OSEventQueue.
            if (pumpStatus.result == WindowPumpResult::Quit)
            {
                exitCode = pumpStatus.exitCode;
                break;
            }

            sceneSystem.NotifyMainThreadFrameEnd();
        }

        return exitCode;
    }

    int Application::RunApplication()
    {
        VE_LOG_INFO("{} starting", initParam_.name);
        ErrorCode runtimeResult = InitializeEngineRuntime();
        if (runtimeResult != ErrorCode::None)
        {
            throw;
        }

        Result<std::unique_ptr<Window>> windowResult = CreateMainWindow();
        if (!windowResult)
        {
            throw;
        }

        std::unique_ptr<Window> mainWindow = windowResult.MoveValue();
        mainWindow->SetCommandHandler([](std::string_view command)
                                      { VE_LOG_INFO_CATEGORY("GM", "Unhandled GM command: {}", command); });

        ErrorCode renderResult = InitializeRendering(*mainWindow);
        if (renderResult != ErrorCode::None)
        {
            throw;
        }

        const int result = RunMainLoop(*mainWindow);
        ShutdownRendering();

        mainWindow->PumpCommands();
        mainWindow->SetCommandHandler({});
        mainWindow.reset();
        ShutdownEngineRuntime(engineRuntime_);

        VE_LOG_INFO("{} stopped with exit code {}", initParam_.name, result);
        return result;
    }

    const std::string& Application::GetName() const noexcept
    {
        return initParam_.name;
    }

    int Application::GetExitCode() const noexcept
    {
        return exitCode_;
    }

    EngineRuntime& Application::GetRuntime() noexcept
    {
        return engineRuntime_;
    }

    const EngineRuntime& Application::GetRuntime() const noexcept
    {
        return engineRuntime_;
    }
} // namespace ve
