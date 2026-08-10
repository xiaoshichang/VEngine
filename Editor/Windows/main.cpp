#include "Editor/Panels/ConsolePanel/ConsolePanel.h"
#include "Editor/Windows/WindowsEditorApplication.h"
#include "Engine/Runtime/Application/ApplicationInitParamOptionParser.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#ifdef GetMessage
#undef GetMessage
#endif

#include <utility>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, PWSTR commandLine, int showCommand)
{
    (void)instance;
    (void)previousInstance;
    (void)commandLine;
    (void)showCommand;

    ve::Result<ve::ApplicationInitParam> initParamResult = ve::ApplicationInitParamOptionParser::ParseCurrentProcessCommandLine();

    ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
    loggingConfig.enableConsole = false;
    ve::ErrorCode loggingResult = ve::InitializeLogging(loggingConfig);
    if (loggingResult != ve::ErrorCode::None)
    {
        return 1;
    }
    ve::SetLogCallback(ve::editor::CaptureEditorLog);

    if (!initParamResult)
    {
        VE_LOG_ERROR_CATEGORY("Application", "Failed to parse command-line options: {}", initParamResult.GetError().GetMessage());
        ve::SetLogCallback(nullptr);
        ve::ShutdownLogging();
        return 1;
    }

    ve::ApplicationInitParam initParam = initParamResult.MoveValue();
    initParam.name = "VEngineWinEditor";
    initParam.mainWindow.title = "VEngine Win Editor";
    initParam.mainWindow.width = 2400;
    initParam.mainWindow.height = 1300;
    initParam.mainWindow.visible = true;
    initParam.runtime.jobSystem.workerThreadNamePrefix = "VEngineWinEditorJobWorker";
    initParam.runtime.ioSystem.threadName = "VEngineWinEditorIOThread";
    initParam.runtime.renderSystem.threadName = "VEngineWinEditorRenderThread";
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
    ve::SetLogCallback(nullptr);
    ve::ShutdownLogging();
    return exitCode;
}
