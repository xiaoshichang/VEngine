#include "Engine/Runtime/Application/Application.h"

#include "Engine/Runtime/Logging/Log.h"

#include <chrono>
#include <cstdio>
#include <array>
#include <thread>
#include <utility>

namespace ve
{
    namespace
    {
        constexpr SizeT MaxWindowOSEventChangesPerFrame = 4;

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

        void EnqueueOSEventToSceneThread(SceneSystem& sceneSystem, OSEvent event)
        {
            ErrorCode queueResult = sceneSystem.EnqueueOSEvent(event);
            if (queueResult != ErrorCode::None)
            {
                VE_LOG_WARN("Failed to enqueue OS event for Scene Thread: {}", ToString(queueResult));
            }
        }

        [[nodiscard]] SizeT CollectWindowStateDeltaEvents(std::array<OSEvent, MaxWindowOSEventChangesPerFrame>& events,
                                                          const WindowStateSnapshot& previousState,
                                                          const WindowStateSnapshot& currentState)
        {
            SizeT eventCount = 0;

            if (previousState.focused != currentState.focused)
            {
                events[eventCount++] = OSEvent{
                    currentState.focused ? OSEventType::WindowFocusGained : OSEventType::WindowFocusLost,
                };
            }

            if (previousState.minimized != currentState.minimized)
            {
                const OSEventType minimizedEventType =
                    currentState.minimized ? OSEventType::WindowMinimized : OSEventType::WindowRestored;
                events[eventCount++] = OSEvent{
                    minimizedEventType,
                };
            }

            if (previousState.visible != currentState.visible)
            {
                events[eventCount++] = OSEvent{currentState.visible ? OSEventType::WindowShown
                                                                    : OSEventType::WindowHidden};
            }

            if (previousState.extent.width != currentState.extent.width ||
                previousState.extent.height != currentState.extent.height)
            {
                events[eventCount++] = OSEvent{
                    OSEventType::WindowResized,
                    currentState.extent.width,
                    currentState.extent.height,
                };
            }

            VE_ASSERT_MESSAGE(eventCount <= events.size(), "CollectWindowStateDeltaEvents overflowed its buffer.");
            return eventCount;
        }

        void EnqueueWindowStateDeltaEventsToSceneThread(
            SceneSystem& sceneSystem,
            const std::array<OSEvent, MaxWindowOSEventChangesPerFrame>& events,
            SizeT eventCount)
        {
            for (SizeT eventIndex = 0; eventIndex < eventCount; ++eventIndex)
            {
                EnqueueOSEventToSceneThread(sceneSystem, events[eventIndex]);
            }
        }

        void EnqueuePendingWindowOSEventsToSceneThread(SceneSystem& sceneSystem, Window& window)
        {
            OSEvent event;
            while (window.TryPopOSEvent(event))
            {
                EnqueueOSEventToSceneThread(sceneSystem, event);
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

    Application::~Application()
    {
        UnInit();
    }

    int Application::Init()
    {
        if (initialized_)
        {
            return exitCode_;
        }

        VE_LOG_INFO("{} starting", initParam_.name);
        ErrorCode runtimeResult = InitializeEngineRuntime();
        VE_ASSERT_MESSAGE(runtimeResult == ErrorCode::None, "InitializeEngineRuntime fail.");
                          
        Result<std::unique_ptr<Window>> windowResult = CreateMainWindow();
        VE_ASSERT_MESSAGE(windowResult.IsOk(), "CreateMainWindow fail");

        mainWindow_ = windowResult.MoveValue();
        mainWindow_->SetCommandHandler([](std::string_view command)
                                       { VE_LOG_INFO_CATEGORY("GM", "Unhandled GM command: {}", command); });

        ErrorCode renderResult = InitializeRendering(*mainWindow_);
        VE_ASSERT_MESSAGE(renderResult == ErrorCode::None, "InitializeRendering fail");

        initialized_ = true;
        exitCode_ = 0;
        return exitCode_;
    }

    void Application::Run()
    {
        exitCode_ = RunMainLoop(*mainWindow_);
    }

    void Application::UnInit()
    {
        const int finalExitCode = exitCode_;
        if (mainWindow_ != nullptr)
        {
            mainThreadCommandQueue_.ExecutePending();
            mainThreadCommandQueue_.Clear();
            mainWindow_->PumpCommands();
            mainWindow_->SetCommandHandler({});
            mainWindow_.reset();
        }

        ShutdownEngineRuntime(engineRuntime_);
        initialized_ = false;
        VE_LOG_INFO("{} stopped with exit code {}", initParam_.name, finalExitCode);
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

    int Application::RunMainLoop(Window& mainWindow)
    {
        int exitCode = 0;
        WindowStateSnapshot previousState = CaptureWindowState(mainWindow);

        SceneSystem& sceneSystem = engineRuntime_.GetSceneSystem();
        sceneSystem.StartLoop();
        while (!mainWindow.ShouldClose())
        {
            mainWindow.PumpCommands();

            // Step 1: pump native window messages. The platform layer mutates the live window state here.
            const WindowPumpStatus pumpStatus = mainWindow.PumpEvents();
            const WindowStateSnapshot currentState = CaptureWindowState(mainWindow);

            // Step 2: detect the window state changes that should be forwarded to the Scene Thread.
            std::array<OSEvent, MaxWindowOSEventChangesPerFrame> windowStateDeltaEvents = {};
            const SizeT windowStateDeltaEventCount =
                CollectWindowStateDeltaEvents(windowStateDeltaEvents, previousState, currentState);

            // Step 3: publish window delta events to the Scene Thread. Viewport, input, and future render-facing
            // state updates are handled there so rendering-related state is not mutated from the Main Thread.
            EnqueueWindowStateDeltaEventsToSceneThread(sceneSystem, windowStateDeltaEvents, windowStateDeltaEventCount);
            EnqueuePendingWindowOSEventsToSceneThread(sceneSystem, mainWindow);
            previousState = currentState;
            mainThreadCommandQueue_.ExecutePending();

            // Step 4: honor WM_QUIT / platform exit requests after the current frame's Main Thread work completes.
            if (pumpStatus.result == WindowPumpResult::Quit)
            {
                exitCode = pumpStatus.exitCode;
                break;
            }

            // Step 5: hand off the completed Main Thread frame to the Scene Thread.
            sceneSystem.NotifyMainThreadFrameEnd();
        }

        return exitCode;
    }

    const std::string& Application::GetName() const noexcept
    {
        return initParam_.name;
    }

    int Application::GetExitCode() const noexcept
    {
        return exitCode_;
    }

    void* Application::GetMainWindowNativeHandle() const noexcept
    {
        if (mainWindow_ == nullptr)
        {
            return nullptr;
        }

        return mainWindow_->GetNativeHandle();
    }

    void* Application::GetMainWindowNativeLayer() const noexcept
    {
        if (mainWindow_ == nullptr)
        {
            return nullptr;
        }

        return mainWindow_->GetNativeLayer();
    }

    EngineRuntime& Application::GetRuntime() noexcept
    {
        return engineRuntime_;
    }

    const EngineRuntime& Application::GetRuntime() const noexcept
    {
        return engineRuntime_;
    }

    ApplicationCommandQueue& Application::GetMainThreadCommandQueue() noexcept
    {
        return mainThreadCommandQueue_;
    }

} // namespace ve
