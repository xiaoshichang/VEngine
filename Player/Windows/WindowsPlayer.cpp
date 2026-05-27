#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Shellapi.h>

#include <string>
#include <string_view>
#include <utility>

namespace
{
    struct PlayerLaunchOptions
    {
        ve::Path projectRoot;
        ve::Path contentRoot;
        ve::Path packageRoot;
        ve::Path scenePath;
    };

    [[nodiscard]] std::string WideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }

        const int requiredLength = WideCharToMultiByte(
            CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (requiredLength <= 0)
        {
            return {};
        }

        std::string utf8Text(static_cast<size_t>(requiredLength), '\0');
        WideCharToMultiByte(CP_UTF8,
                            0,
                            text.data(),
                            static_cast<int>(text.size()),
                            utf8Text.data(),
                            requiredLength,
                            nullptr,
                            nullptr);
        return utf8Text;
    }

    [[nodiscard]] PlayerLaunchOptions ParsePlayerArguments()
    {
        PlayerLaunchOptions options;

        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            return options;
        }

        for (int index = 1; index < argumentCount; ++index)
        {
            const std::wstring_view argument(arguments[index]);
            if (argument == L"--project" && index + 1 < argumentCount)
            {
                options.projectRoot = ve::Path(WideToUtf8(arguments[++index]));
            }
            else if (argument == L"--content" && index + 1 < argumentCount)
            {
                options.contentRoot = ve::Path(WideToUtf8(arguments[++index]));
            }
            else if (argument == L"--package" && index + 1 < argumentCount)
            {
                options.packageRoot = ve::Path(WideToUtf8(arguments[++index]));
            }
            else if (argument == L"--scene" && index + 1 < argumentCount)
            {
                options.scenePath = ve::Path(WideToUtf8(arguments[++index]));
            }
        }

        LocalFree(arguments);
        return options;
    }

    [[nodiscard]] ve::Path ResolveLaunchProjectRoot(const PlayerLaunchOptions& options)
    {
        if (!options.contentRoot.IsEmpty())
        {
            return options.contentRoot;
        }

        if (!options.packageRoot.IsEmpty())
        {
            return options.packageRoot / "Content";
        }

        if (!options.projectRoot.IsEmpty())
        {
            return options.projectRoot;
        }

        const ve::Path adjacentContentRoot = ve::FileSystem::GetExecutableDirectory() / "Content";
        if (ve::FileSystem::IsFile(adjacentContentRoot / ".veproject"))
        {
            return adjacentContentRoot;
        }

        return {};
    }
} // namespace

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

    const PlayerLaunchOptions launchOptions = ParsePlayerArguments();

    ve::ApplicationDesc desc;
    desc.name = "VEnginePlayer";
    desc.mainWindow.title = "VEngine Player";
    desc.mainWindow.width = 1280;
    desc.mainWindow.height = 720;
    desc.mainWindow.visible = true;
    desc.projectRoot = ResolveLaunchProjectRoot(launchOptions);
    desc.sampleScenePath = launchOptions.scenePath;
    desc.runtime.jobSystem.workerThreadNamePrefix = "VEnginePlayerJobWorker";
    desc.runtime.ioSystem.threadName = "VEnginePlayerIOThread";
    desc.runtime.renderSystem.threadName = "VEnginePlayerRenderThread";
    desc.runtime.gameThreadSystem.threadName = "VEnginePlayerGameThread";

    if (!desc.projectRoot.IsEmpty())
    {
        VE_LOG_INFO_CATEGORY("Player", "Launch content root: {}", desc.projectRoot.GetString());
    }
    if (!desc.sampleScenePath.IsEmpty())
    {
        VE_LOG_INFO_CATEGORY("Player", "Launch scene override: {}", desc.sampleScenePath.GetString());
    }

    ve::Application application(std::move(desc));
    const int exitCode = application.Run();
    ve::ShutdownLogging();
    return exitCode;
}
