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
    : desc_()
{
    desc_.name = std::move(name);

    if (desc_.name.empty())
    {
        desc_.name = "VEngine";
    }

    desc_.mainWindow.title = desc_.name;
}

Application::Application(ApplicationDesc desc)
    : desc_(std::move(desc))
{
    if (desc_.name.empty())
    {
        desc_.name = "VEngine";
    }

    if (desc_.mainWindow.title.empty())
    {
        desc_.mainWindow.title = desc_.name;
    }
}

Application::~Application() = default;

int Application::Run()
{
    exitCode_ = RunApplication();
    return exitCode_;
}

Result<void> Application::InitializeEngineRuntime()
{
    Result<void> runtimeResult = engineRuntime_.Initialize(desc_.runtime);

    if (!runtimeResult)
    {
        VE_LOG_ERROR("Failed to initialize engine runtime: {}", runtimeResult.GetError().GetMessage());
    }

    return runtimeResult;
}

Result<std::unique_ptr<Window>> Application::CreateMainWindow()
{
    Result<std::unique_ptr<Window>> windowResult = Window::Create(desc_.mainWindow);

    if (!windowResult)
    {
        VE_LOG_ERROR("Failed to create main window: {}", windowResult.GetError().GetMessage());
    }

    return windowResult;
}

Result<void> Application::InitializeRendering(Window& mainWindow)
{
    RenderSystem& renderSystem = engineRuntime_.GetRenderSystem();

    Result<void> deviceResult = renderSystem.InitializeDevice(desc_.runtime.renderSystem.device);
    if (!deviceResult)
    {
        VE_LOG_ERROR("Failed to initialize render device: {}", deviceResult.GetError().GetMessage());
        return deviceResult;
    }

    const WindowExtent extent = mainWindow.GetClientExtent();
    RenderSurfaceDesc surfaceDesc;
    surfaceDesc.nativeWindow = mainWindow.GetNativeHandle();
    surfaceDesc.nativeLayer = mainWindow.GetNativeLayer();
    surfaceDesc.width = extent.width;
    surfaceDesc.height = extent.height;

    Result<void> swapchainResult = renderSystem.CreateMainSwapchain(surfaceDesc);
    if (!swapchainResult)
    {
        VE_LOG_ERROR("Failed to create main render swapchain: {}", swapchainResult.GetError().GetMessage());
        renderSystem.ShutdownDevice();
        return swapchainResult;
    }

    return Result<void>::Success();
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

        Result<void> renderResult = renderSystem.RenderFrame();
        if (!renderResult)
        {
            VE_LOG_ERROR("RenderFrame failed: {}", renderResult.GetError().GetMessage());
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return exitCode;
}

int Application::RunApplication()
{
    VE_LOG_INFO("{} starting", desc_.name);

    auto cleanup = [this](std::unique_ptr<Window>& window)
    {
        if (window != nullptr)
        {
            window->PumpCommands();
            window->SetCommandHandler({});
        }

        window.reset();
        ShutdownEngineRuntime(engineRuntime_);
    };

    Result<void> runtimeResult = InitializeEngineRuntime();
    if (!runtimeResult)
    {
        std::unique_ptr<Window> emptyWindow;
        cleanup(emptyWindow);
        return 1;
    }

    Result<std::unique_ptr<Window>> windowResult = CreateMainWindow();
    if (!windowResult)
    {
        std::unique_ptr<Window> emptyWindow;
        cleanup(emptyWindow);
        return 1;
    }

    std::unique_ptr<Window> mainWindow = windowResult.MoveValue();
    mainWindow->SetCommandHandler([](std::string_view command)
    {
        VE_LOG_INFO_CATEGORY("GM", "Unhandled GM command: {}", command);
    });

    Result<void> renderResult = InitializeRendering(*mainWindow);
    if (!renderResult)
    {
        cleanup(mainWindow);
        return 1;
    }

    const int result = RunMainLoop(*mainWindow);
    ShutdownRendering();
    cleanup(mainWindow);

    VE_LOG_INFO("{} stopped with exit code {}", desc_.name, result);
    return result;
}

const std::string& Application::GetName() const noexcept
{
    return desc_.name;
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
