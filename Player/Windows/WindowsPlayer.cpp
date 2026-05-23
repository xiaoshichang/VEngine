#include "Engine/Runtime/Application/Application.h"
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

    ve::ApplicationDesc desc;
    desc.name = "VEnginePlayer";
    desc.mainWindow.title = "VEngine Player";
    desc.mainWindow.width = 1280;
    desc.mainWindow.height = 720;
    desc.mainWindow.visible = true;
    desc.runtime.jobSystem.workerThreadNamePrefix = "VEnginePlayerJobWorker";

    ve::Application application(std::move(desc));
    return application.Run();
}
