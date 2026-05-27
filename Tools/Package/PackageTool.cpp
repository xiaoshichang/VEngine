#include "Tools/Package/PackageService.h"

#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/Logging/Log.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    struct CommandLine
    {
        std::vector<std::string_view> args;
    };

    void PrintHelp()
    {
        const ve::BuildInfo buildInfo = ve::GetBuildInfo();
        std::cout << "VEnginePackageTool (" << buildInfo.projectName << " " << buildInfo.version << ")\n";
        std::cout << "Commands:\n";
        std::cout << "  package --project <project-root> --platform Windows|iOS --config Debug|Release "
                     "[--output <output-dir>] [--player <VEnginePlayer.exe>] [--no-runtime]\n";
    }

    [[nodiscard]] std::string_view FindOption(const CommandLine& commandLine, std::string_view option)
    {
        for (size_t index = 0; index + 1 < commandLine.args.size(); ++index)
        {
            if (commandLine.args[index] == option)
            {
                return commandLine.args[index + 1];
            }
        }

        return {};
    }

    [[nodiscard]] bool HasOption(const CommandLine& commandLine, std::string_view option)
    {
        for (std::string_view arg : commandLine.args)
        {
            if (arg == option)
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] int RunPackage(const CommandLine& commandLine)
    {
        const std::string_view project = FindOption(commandLine, "--project");
        const std::string_view platformText = FindOption(commandLine, "--platform");
        const std::string_view configText = FindOption(commandLine, "--config");
        const std::string_view output = FindOption(commandLine, "--output");
        if (project.empty() || platformText.empty() || configText.empty())
        {
            std::cerr << "Missing required package option.\n";
            return 1;
        }

        ve::Result<ve::PackagePlatform> platform = ve::ParsePackagePlatform(platformText);
        if (!platform)
        {
            std::cerr << platform.GetError().GetMessage() << '\n';
            return 1;
        }

        ve::Result<ve::PackageConfiguration> configuration = ve::ParsePackageConfiguration(configText);
        if (!configuration)
        {
            std::cerr << configuration.GetError().GetMessage() << '\n';
            return 1;
        }

        ve::PackageRequest request;
        request.projectRoot = ve::Path(project);
        request.outputRoot = ve::Path(output);
        request.platform = platform.GetValue();
        request.configuration = configuration.GetValue();
        request.playerExecutable = ve::Path(FindOption(commandLine, "--player"));
        request.includeRuntimeBinaries = !HasOption(commandLine, "--no-runtime");

        ve::Result<ve::PackageResult> packageResult = ve::StagePackage(request);
        if (!packageResult)
        {
            std::cerr << "Package failed: " << packageResult.GetError().GetMessage() << '\n';
            return 1;
        }

        const ve::PackageResult& result = packageResult.GetValue();
        std::cout << "Packaged " << project << '\n';
        std::cout << "  package: " << result.packageRoot.GetString() << '\n';
        std::cout << "  content: " << result.contentRoot.GetString() << '\n';
        std::cout << "  manifest: " << result.manifestPath.GetString() << '\n';
        std::cout << "  assets: " << result.assetCount << '\n';
        std::cout << "  artifacts: " << result.artifactCount << '\n';
        std::cout << "  files: " << result.stagedFiles.size() << '\n';
        return 0;
    }
} // namespace

int main(int argc, char* argv[])
{
    ve::LoggingConfig loggingConfig = ve::MakeDefaultLoggingConfig();
    loggingConfig.enableFile = false;
    const ve::ErrorCode loggingResult = ve::InitializeLogging(loggingConfig);
    if (loggingResult != ve::ErrorCode::None)
    {
        std::cerr << "Failed to initialize logging: " << ve::ToString(loggingResult) << '\n';
        return 1;
    }

    CommandLine commandLine;
    for (int index = 1; index < argc; ++index)
    {
        commandLine.args.emplace_back(argv[index]);
    }

    int exitCode = 0;
    if (commandLine.args.empty() || commandLine.args[0] == "--help" || commandLine.args[0] == "help")
    {
        PrintHelp();
    }
    else if (commandLine.args[0] == "package")
    {
        exitCode = RunPackage(commandLine);
    }
    else
    {
        std::cerr << "Unknown command: " << commandLine.args[0] << '\n';
        PrintHelp();
        exitCode = 1;
    }

    ve::ShutdownLogging();
    return exitCode;
}
