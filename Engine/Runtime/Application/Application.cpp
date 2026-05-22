#include "Engine/Runtime/Application/Application.h"

#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Time/Time.h"

#if VE_PLATFORM_WINDOWS
#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"
#include "Engine/Runtime/Platform/Windows/Win32MessageLoop.h"
#include "Engine/Runtime/Platform/Windows/Win32Window.h"
#endif

#include <chrono>
#include <thread>
#include <utility>

namespace ve
{
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
    bool ownsLogging = false;
    bool assertionLogInstalled = false;

    if (!IsLoggingInitialized())
    {
        Result<void> loggingResult = InitializeLogging();

        if (!loggingResult)
        {
            exitCode_ = 1;
            return exitCode_;
        }

        ownsLogging = true;
    }

    Result<void> assertionLogResult = InstallAssertionLogHandler();

    if (assertionLogResult)
    {
        assertionLogInstalled = true;
    }
    else
    {
        VE_LOG_DEBUG("Assertion log handler was not installed: {}", assertionLogResult.GetError().GetMessage());
    }

#if VE_PLATFORM_WINDOWS
    VE_LOG_INFO("{} starting", desc_.name);

    SetWin32DebugConsoleCommandHandler([](std::string_view command)
    {
        VE_LOG_INFO_CATEGORY("GM", "Unhandled GM command: {}", command);
    });

    Result<std::unique_ptr<Win32Window>> windowResult = Win32Window::Create(desc_.mainWindow);

    if (!windowResult)
    {
        VE_LOG_ERROR("Failed to create main window: {}", windowResult.GetError().GetMessage());
        exitCode_ = 1;

        if (assertionLogInstalled && !ownsLogging)
        {
            UninstallAssertionLogHandler();
        }

        SetWin32DebugConsoleCommandHandler({});

        if (ownsLogging)
        {
            ShutdownLogging();
        }

        return exitCode_;
    }

    std::unique_ptr<Win32Window> mainWindow = windowResult.MoveValue();
    Win32MessageLoop messageLoop;
    Time::Reset();

    while (!mainWindow->ShouldClose())
    {
        PumpWin32DebugConsoleCommands();

        if (messageLoop.PumpPendingMessages() == Win32MessageLoop::PumpResult::Quit)
        {
            break;
        }

        Time::Tick();

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    exitCode_ = messageLoop.GetQuitExitCode();
    PumpWin32DebugConsoleCommands();
    SetWin32DebugConsoleCommandHandler({});
    mainWindow.reset();

    VE_LOG_INFO("{} stopped with exit code {}", desc_.name, exitCode_);
#else
    VE_LOG_ERROR("{} cannot run because this platform does not have an Application backend yet.", desc_.name);
    exitCode_ = 1;
#endif

    if (assertionLogInstalled && !ownsLogging)
    {
        UninstallAssertionLogHandler();
    }

    if (ownsLogging)
    {
        ShutdownLogging();
    }

    return exitCode_;
}

const std::string& Application::GetName() const noexcept
{
    return desc_.name;
}

int Application::GetExitCode() const noexcept
{
    return exitCode_;
}
}
