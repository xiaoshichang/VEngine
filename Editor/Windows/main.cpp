#include "Editor/Windows/WindowsEditorApplication.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/DebugConsole.h"

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
    initParam.name = "VEngineWinEditor";
    initParam.mainWindow.title = "VEngine Win Editor";
    initParam.mainWindow.width = 2400;
    initParam.mainWindow.height = 1200;
    initParam.mainWindow.visible = true;
    initParam.runtime.jobSystem.workerThreadNamePrefix = "VEngineWinEditorJobWorker";
    initParam.runtime.ioSystem.threadName = "VEngineWinEditorIOThread";
    initParam.runtime.renderSystem.threadName = "VEngineWinEditorRenderThread";
    initParam.runtime.renderSystem.device.backend = ve::RenderBackend::D3D11;
    initParam.runtime.scriptingSystem.scriptHostRoot = ve::FileSystem::GetExecutableDirectory() / "Managed" / "VEngine.ScriptHost";
    initParam.runtime.scriptingSystem.runtimeConfigPath = initParam.runtime.scriptingSystem.scriptHostRoot / "VEngine.ScriptHost.runtimeconfig.json";
    const ve::Path editorLocalDotNetRoot = ve::FileSystem::GetExecutableDirectory() / "DotNet" / "win-x64" / "10.0.9";
    if (ve::FileSystem::IsFile(editorLocalDotNetRoot / "dotnet.exe"))
    {
        initParam.runtime.scriptingSystem.dotNetRuntimeRoot = editorLocalDotNetRoot;
    }

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
