#include "Engine/Runtime/Application/Application.h"

#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Time/Time.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

namespace ve
{
namespace
{
void ShutdownEngineRuntime(EngineRuntime& runtime)
{
    runtime.Shutdown();
}
}

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
    Time::Reset();
    int exitCode = 0;

    while (!mainWindow.ShouldClose())
    {
        mainWindow.PumpCommands();

        const WindowPumpStatus pumpStatus = mainWindow.PumpEvents();
        if (pumpStatus.result == WindowPumpResult::Quit)
        {
            exitCode = pumpStatus.exitCode;
            break;
        }

        Time::Tick();

        ErrorCode renderResult = renderSystem.RenderFrame();
        if (renderResult != ErrorCode::None)
        {
            VE_LOG_ERROR("RenderFrame failed: {}", ToString(renderResult));
            return 1;
        }
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
    {
        VE_LOG_INFO_CATEGORY("GM", "Unhandled GM command: {}", command);
    });

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
}
