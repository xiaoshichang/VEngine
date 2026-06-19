#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"
#include "Player/Windows/WindowsPlayer.h"

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

    ve::ApplicationInitParam initParam;
    initParam.name = "VEnginePlayer";
    initParam.mainWindow.title = "VEngine Player";
    initParam.mainWindow.width = 1280;
    initParam.mainWindow.height = 720;
    initParam.mainWindow.visible = true;
    initParam.runtime.jobSystem.workerThreadNamePrefix = "VEnginePlayerJobWorker";
    initParam.runtime.ioSystem.threadName = "VEnginePlayerIOThread";
    initParam.runtime.renderSystem.threadName = "VEnginePlayerRenderThread";

    ve::WindowsPlayer application(std::move(initParam));
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
