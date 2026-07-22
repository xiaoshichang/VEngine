#include "Editor/macOS/MacEditorApplication.h"
#include "Editor/Core/EditorStartup.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/DebugConsole.h"

#import <AppKit/AppKit.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        ve::InitializeDebugConsole();

        ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
        const char* homePath = std::getenv("HOME");
        if (homePath != nullptr && homePath[0] != '\0')
        {
            loggingConfig.filePath = std::filesystem::path(homePath) / "Library" / "Application Support" / "VEngine" / "Editor" / "Logs" / "VEngine.log";
        }

        if (ve::InitializeLogging(loggingConfig) != ve::ErrorCode::None)
        {
            return 1;
        }

        ve::ApplicationInitParam initParam;
        initParam.name = "VEngineEditor";
        initParam.mainWindow.title = "VEngine Editor";
        initParam.mainWindow.width = 1600;
        initParam.mainWindow.height = 1000;
        initParam.mainWindow.visible = true;
        initParam.runtime.renderSystem.device.backend = ve::RenderBackend::Metal;

        const ve::Path executableDirectory = ve::FileSystem::GetExecutableDirectory();
        const ve::Path bundleContentsDirectory = executableDirectory.GetParentPath();
        initParam.runtime.scriptingSystem.scriptHostRoot = bundleContentsDirectory / "Resources" / "Managed" / "VEngine.ScriptHost";
        initParam.runtime.scriptingSystem.runtimeConfigPath =
            initParam.runtime.scriptingSystem.scriptHostRoot / "VEngine.ScriptHost.runtimeconfig.json";
        initParam.runtime.scriptingSystem.dotNetRuntimeRoot = bundleContentsDirectory / "Resources" / "DotNet" / "osx-arm64" / "10.0.9";

        std::vector<std::string> arguments;
        arguments.reserve(static_cast<size_t>(argc));
        for (int argumentIndex = 0; argumentIndex < argc; ++argumentIndex)
        {
            arguments.emplace_back(argv[argumentIndex] == nullptr ? "" : argv[argumentIndex]);
        }
        ve::editor::EditorStartupOptions startupOptions = ve::editor::ParseEditorStartupOptions(arguments);
        ve::editor::MacEditorApplication application(std::move(initParam), std::move(startupOptions));
        int exitCode = application.Init();
        if (exitCode == 0)
        {
            [NSApp activateIgnoringOtherApps:YES];
            application.Run();
            exitCode = application.GetExitCode();
        }
        application.UnInit();
        ve::ShutdownLogging();
        return exitCode;
    }
}
