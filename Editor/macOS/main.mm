#include "Editor/Panels/ConsolePanel/ConsolePanel.h"
#include "Editor/macOS/MacEditorApplication.h"
#include "Engine/Runtime/Application/ApplicationInitParamOptionParser.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Logging/Log.h"

#import <AppKit/AppKit.h>
#include <cstdlib>
#include <filesystem>

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        ve::Result<ve::ApplicationInitParam> initParamResult = ve::ApplicationInitParamOptionParser::Parse(argc, argv);

        ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
        loggingConfig.enableConsole = false;
        const char* homePath = std::getenv("HOME");
        if (homePath != nullptr && homePath[0] != '\0')
        {
            loggingConfig.filePath = std::filesystem::path(homePath) / "Library" / "Application Support" / "VEngine" / "Editor" / "Logs" / "VEngine.log";
        }

        if (ve::InitializeLogging(loggingConfig) != ve::ErrorCode::None)
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

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        ve::ApplicationInitParam initParam = initParamResult.MoveValue();
        initParam.name = "VEngineEditor";
        initParam.mainWindow.title = "VEngine Editor";
        initParam.mainWindow.width = 1600;
        initParam.mainWindow.height = 1300;
        initParam.mainWindow.visible = true;

        const ve::Path executableDirectory = ve::FileSystem::GetExecutableDirectory();
        const ve::Path bundleContentsDirectory = executableDirectory.GetParentPath();
        initParam.runtime.scriptingSystem.scriptHostRoot = bundleContentsDirectory / "Resources" / "Managed" / "VEngine.ScriptHost";
        initParam.runtime.scriptingSystem.runtimeConfigPath = initParam.runtime.scriptingSystem.scriptHostRoot / "VEngine.ScriptHost.runtimeconfig.json";
        initParam.runtime.scriptingSystem.dotNetRuntimeRoot = bundleContentsDirectory / "Resources" / "DotNet" / "osx-arm64" / "10.0.9";

        ve::editor::MacEditorApplication application(std::move(initParam));
        int exitCode = application.Init();
        if (exitCode == 0)
        {
            [NSApp activateIgnoringOtherApps:YES];
            application.Run();
            exitCode = application.GetExitCode();
        }
        application.UnInit();
        ve::SetLogCallback(nullptr);
        ve::ShutdownLogging();
        return exitCode;
    }
}
