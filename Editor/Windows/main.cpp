#include "Editor/Windows/WindowsEditorApplication.h"
#include "Editor/Core/EditorStartup.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/DebugConsole.h"
#include "Engine/Runtime/Platform/Windows/Win32RenderBackendSelection.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] std::string WideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int requiredLength = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (requiredLength <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(requiredLength), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), requiredLength, nullptr, nullptr) <= 0)
        {
            return {};
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string> GetUtf8CommandLineArguments()
    {
        int argumentCount = 0;
        LPWSTR* nativeArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (nativeArguments == nullptr)
        {
            return {};
        }

        std::vector<std::string> arguments;
        arguments.reserve(static_cast<size_t>(argumentCount));
        for (int argumentIndex = 0; argumentIndex < argumentCount; ++argumentIndex)
        {
            arguments.push_back(WideToUtf8(nativeArguments[argumentIndex]));
        }
        LocalFree(nativeArguments);
        return arguments;
    }
} // namespace

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
    initParam.mainWindow.height = 800;
    initParam.mainWindow.visible = true;
    initParam.runtime.jobSystem.workerThreadNamePrefix = "VEngineWinEditorJobWorker";
    initParam.runtime.ioSystem.threadName = "VEngineWinEditorIOThread";
    initParam.runtime.renderSystem.threadName = "VEngineWinEditorRenderThread";
    initParam.runtime.renderSystem.device.backend = ve::SelectWin32RenderBackendFromCommandLine();
    initParam.runtime.scriptingSystem.scriptHostRoot = ve::FileSystem::GetExecutableDirectory() / "Managed" / "VEngine.ScriptHost";
    initParam.runtime.scriptingSystem.runtimeConfigPath = initParam.runtime.scriptingSystem.scriptHostRoot / "VEngine.ScriptHost.runtimeconfig.json";
    const ve::Path editorLocalDotNetRoot = ve::FileSystem::GetExecutableDirectory() / "DotNet" / "win-x64" / "10.0.9";
    if (ve::FileSystem::IsFile(editorLocalDotNetRoot / "dotnet.exe"))
    {
        initParam.runtime.scriptingSystem.dotNetRuntimeRoot = editorLocalDotNetRoot;
    }

    const std::vector<std::string> arguments = GetUtf8CommandLineArguments();
    ve::editor::EditorStartupOptions startupOptions = ve::editor::ParseEditorStartupOptions(arguments);
    ve::editor::WindowsEditorApplication application(std::move(initParam), std::move(startupOptions));
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
