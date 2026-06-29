#include "Editor/macOS/MacEditorApplication.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/DebugConsole.h"

#import <AppKit/AppKit.h>

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

        if (ve::InitializeLogging() != ve::ErrorCode::None)
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
        initParam.runtime.scriptingSystem.scriptHostRoot = executableDirectory.GetParentPath() / "VEngineMacEditor.Managed" / "VEngine.ScriptHost";

        ve::editor::MacEditorApplication application(std::move(initParam));
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
