#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <utility>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, PWSTR commandLine, int showCommand)
{
    (void)instance;
    (void)previousInstance;
    (void)commandLine;
    (void)showCommand;

    ve::InitializeWin32DebugConsole();

    ve::ErrorCode loggingResult = ve::InitializeLogging();
    if (loggingResult != ve::ErrorCode::None)
    {
        return 1;
    }

    ve::ApplicationInitParam desc;
    desc.name = "VEnginePlayer";
    desc.mainWindow.title = "VEngine Player";
    desc.mainWindow.width = 1280;
    desc.mainWindow.height = 720;
    desc.mainWindow.visible = true;
    desc.runtime.jobSystem.workerThreadNamePrefix = "VEnginePlayerJobWorker";
    desc.runtime.ioSystem.threadName = "VEnginePlayerIOThread";
    desc.runtime.renderSystem.threadName = "VEnginePlayerRenderThread";

    ve::Application application(std::move(desc));
    int exitCode = application.Init();
    if (exitCode == 0)
    {
        application.Run();
        exitCode = application.GetExitCode();
    }
    application.UnInit();
    ve::ShutdownLogging();
    return exitCode;
}
