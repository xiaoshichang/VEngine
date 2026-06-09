#include "Editor/Windows/WindowsEditorApplication.h"
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

    ve::ApplicationInitParam initParam;
    initParam.name = "VEngineEditor";
    initParam.mainWindow.title = "VEngine Editor";
    initParam.mainWindow.width = 1600;
    initParam.mainWindow.height = 900;
    initParam.mainWindow.visible = true;
    initParam.runtime.jobSystem.workerThreadNamePrefix = "VEngineEditorJobWorker";
    initParam.runtime.ioSystem.threadName = "VEngineEditorIOThread";
    initParam.runtime.renderSystem.threadName = "VEngineEditorRenderThread";

    ve::editor::WindowsEditorApplication application(std::move(initParam));
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
