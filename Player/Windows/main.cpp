#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Platform/DebugConsole.h"
#include "Player/Windows/VEnginePlayer.h"

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

    ve::InitializeDebugConsole();

    ve::ErrorCode loggingResult = ve::InitializeLogging();
    if (loggingResult != ve::ErrorCode::None)
    {
        return 1;
    }

    ve::ApplicationInitParam initParam;
    initParam.name = "VEngineWinPlayer";
    initParam.mainWindow.title = "VEngine Win Player";
    initParam.mainWindow.width = 1280;
    initParam.mainWindow.height = 720;
    initParam.mainWindow.visible = true;
    initParam.runtime.jobSystem.workerThreadNamePrefix = "VEngineWinPlayerJobWorker";
    initParam.runtime.ioSystem.threadName = "VEngineWinPlayerIOThread";
    initParam.runtime.renderSystem.threadName = "VEngineWinPlayerRenderThread";
    initParam.runtime.renderSystem.device.backend = ve::RenderBackend::D3D11;
    initParam.runtime.scriptingSystem.runtimeConfigPath =
        ve::FileSystem::GetExecutableDirectory() / "Managed" / "VEngine.ScriptHost" / "VEngine.ScriptHost.runtimeconfig.json";
    const ve::Path playerLocalDotNetRoot = ve::FileSystem::GetExecutableDirectory() / "DotNet" / "win-x64" / "10.0.9";
    if (ve::FileSystem::IsFile(playerLocalDotNetRoot / "dotnet.exe"))
    {
        initParam.runtime.scriptingSystem.dotNetRuntimeRoot = playerLocalDotNetRoot;
    }

    ve::VEnginePlayer application(std::move(initParam));
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
