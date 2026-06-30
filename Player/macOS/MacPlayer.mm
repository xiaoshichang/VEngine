#include "Player/Windows/VEnginePlayer.h"
#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/DebugConsole.h"

#import <AppKit/AppKit.h>

#include <cstdlib>
#include <filesystem>

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        (void)argc;
        (void)argv;

        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];

        ve::InitializeDebugConsole();

        ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
        const char* homePath = std::getenv("HOME");
        if (homePath != nullptr && homePath[0] != '\0')
        {
            loggingConfig.filePath = std::filesystem::path(homePath) / "Library" / "Application Support" / "VEngine" / "Player" / "Logs" / "VEngine.log";
        }

        if (ve::InitializeLogging(loggingConfig) != ve::ErrorCode::None)
        {
            return 1;
        }

        ve::ApplicationInitParam initParam;
        initParam.name = "VEngineMacPlayer";
        initParam.mainWindow.title = "VEngine Mac Player";
        initParam.mainWindow.width = 1280;
        initParam.mainWindow.height = 720;
        initParam.mainWindow.visible = true;
        initParam.runtime.jobSystem.workerThreadNamePrefix = "VEngineMacPlayerJobWorker";
        initParam.runtime.ioSystem.threadName = "VEngineMacPlayerIOThread";
        initParam.runtime.renderSystem.threadName = "VEngineMacPlayerRenderThread";
        initParam.runtime.renderSystem.device.backend = ve::RenderBackend::Metal;

        const ve::Path executableDirectory = ve::FileSystem::GetExecutableDirectory();
        const ve::Path bundleContentsDirectory = executableDirectory.GetParentPath();
        initParam.runtime.scriptingSystem.scriptHostRoot = bundleContentsDirectory / "Resources" / "Managed" / "VEngine.ScriptHost";
        initParam.runtime.scriptingSystem.runtimeConfigPath =
            initParam.runtime.scriptingSystem.scriptHostRoot / "VEngine.ScriptHost.runtimeconfig.json";
        initParam.runtime.scriptingSystem.dotNetRuntimeRoot = bundleContentsDirectory / "Resources" / "DotNet" / "osx-arm64" / "10.0.9";

        ve::VEnginePlayer engineApplication(std::move(initParam));
        int exitCode = engineApplication.Init();
        if (exitCode == 0)
        {
            [NSApp activateIgnoringOtherApps:YES];
            engineApplication.Run();
            exitCode = engineApplication.GetExitCode();
        }
        engineApplication.UnInit();

        const ve::BuildInfo buildInfo = ve::GetBuildInfo();
        (void)buildInfo;

        ve::ShutdownLogging();
        return exitCode;
    }
}
